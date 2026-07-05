#include "esp_fieldmap.h"

#include <stdio.h>
#include <string.h>

#include "sc_crc.h"
#include "sc_diff.h"

static int hexval(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse one frame line's hex nibbles (ignoring spaces/commas) into `out`, up to
 * ESP_FIELDMAP_MAX_BYTES. Returns the byte count, or -1 on an odd/invalid nibble run. */
static int parse_hex_frame(const char* s, const char* end, uint8_t* out) {
    int nb = 0, hi = -1;
    for(const char* p = s; p < end; p++) {
        char c = *p;
        if(c == ' ' || c == ',' || c == '\t' || c == '\r') continue;
        int v = hexval(c);
        if(v < 0) return -1;
        if(hi < 0) {
            hi = v;
        } else {
            if(nb >= ESP_FIELDMAP_MAX_BYTES) return -1;
            out[nb++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    if(hi >= 0) return -1; /* dangling nibble */
    return nb;
}

size_t esp_fieldmap_parse_hex(
    const char* text, uint8_t* out, size_t max_frames, size_t* nbytes_out) {
    if(nbytes_out) *nbytes_out = 0;
    if(!text || !out || max_frames == 0) return 0;

    size_t nframes = 0;
    size_t nbytes = 0;
    const char* p = text;
    while(*p && nframes < max_frames) {
        const char* start = p;
        while(*p && *p != '\n' && *p != ';') p++;
        const char* end = p;
        if(*p) p++; /* step past the separator */

        const char* q = start; /* skip a blank/whitespace-only line */
        while(q < end && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
        if(q == end) continue;

        uint8_t frame[ESP_FIELDMAP_MAX_BYTES];
        int nb = parse_hex_frame(start, end, frame);
        if(nb <= 0) return 0; /* reject a malformed corpus outright */
        if(nframes == 0) {
            nbytes = (size_t)nb;
        } else if((size_t)nb != nbytes) {
            return 0; /* frames must be aligned (equal length) */
        }
        memcpy(out + nframes * ESP_FIELDMAP_MAX_BYTES, frame, nbytes);
        nframes++;
    }
    if(nframes == 0) return 0;
    if(nbytes_out) *nbytes_out = nbytes;
    return nframes;
}

/* If the trailing byte is a checksum of the preceding bytes CONSISTENTLY across the whole corpus,
 * fill *spec + return true (System §7b tier 2). sc_fieldmap_from_diff can't do this — it has only
 * one frame's profile, not the corpus — so the naming happens here (sc_checksum_search + verify). */
static bool discover_checksum(
    const uint8_t* frames, size_t n_frames, size_t nbytes, ScChecksumSpec* spec) {
    if(n_frames < 2 || nbytes < 2) return false;
    const uint8_t* f0 = frames;
    if(!sc_checksum_search(f0, nbytes - 1, f0[nbytes - 1], spec)) return false;
    for(size_t i = 0; i < n_frames; i++) {
        const uint8_t* f = frames + i * ESP_FIELDMAP_MAX_BYTES;
        if(sc_checksum_compute(spec, f, nbytes - 1) != f[nbytes - 1]) return false;
    }
    return true;
}

bool esp_fieldmap_analyze(
    const uint8_t* frames, size_t n_frames, size_t nbytes, const char* protocol,
    uint8_t modulation, ScFieldMap* out, float* confidence) {
    if(confidence) *confidence = 0.0f;
    if(!frames || !out) return false;
    if(n_frames < 2 || nbytes < 1 || nbytes > ESP_FIELDMAP_MAX_BYTES) return false;

    /* differential bitfield analysis (System §7b tier 1). Corpus is stored at a fixed
     * ESP_FIELDMAP_MAX_BYTES stride; pass that as the stride and nbytes*8 used bits. */
    static ScBitProfile prof[ESP_FIELDMAP_MAX_BYTES * 8];
    sc_diff_analyze(frames, n_frames, nbytes * 8, ESP_FIELDMAP_MAX_BYTES, prof);

    /* seed the proposed field map (byte-granular segments) from the diff profile (shared/core) */
    sc_fieldmap_from_diff(prof, nbytes * 8, protocol, modulation, out);

    /* name the checksum over the corpus + tag the trailing field CHECKSUM (System §7b tier 2) */
    if(discover_checksum(frames, n_frames, nbytes, &out->checksum)) {
        out->has_checksum = true;
        out->checksum_over_bytes = (uint16_t)(nbytes - 1);
        if(out->n_fields > 0) out->fields[out->n_fields - 1].cls = SC_FIELD_CHECKSUM;
    }

    /* proposal confidence: more frames + a named checksum => higher (System §8 field_maps shape).
     * Ground-truth correlation (tier 3) is host/Pi-side (HA/MQTT co-located), so it isn't folded. */
    if(confidence) {
        float conf = 0.3f + 0.05f * (float)n_frames + (out->has_checksum ? 0.2f : 0.0f);
        if(conf > 1.0f) conf = 1.0f;
        *confidence = conf;
    }
    return true;
}

static int count_class(const ScFieldMap* m, uint8_t cls) {
    int n = 0;
    for(size_t i = 0; i < m->n_fields; i++)
        if(m->fields[i].cls == cls) n++;
    return n;
}

/* Emit `s` as a JSON string, escaping the characters that matter for our labels. */
static int json_str(const char* s, char* out, size_t cap) {
    size_t w = 0;
    if(w < cap) out[w] = '"';
    w++;
    for(const char* p = s; *p; p++) {
        char c = *p;
        if(c == '"' || c == '\\') {
            if(w < cap) out[w] = '\\';
            w++;
        }
        if(c == '\n' || c == '\r') c = ' ';
        if(w < cap) out[w] = c;
        w++;
    }
    if(w < cap) out[w] = '"';
    w++;
    return (int)w;
}

int esp_fieldmap_to_json(const ScFieldMap* map, float confidence, char* out, size_t cap) {
    if(!map || !out) return -1;
    int w = 0;
#define APP(...)                                                             \
    do {                                                                     \
        int _n = snprintf(out + w, (w < (int)cap) ? cap - w : 0, __VA_ARGS__); \
        if(_n < 0) return -1;                                                \
        w += _n;                                                             \
    } while(0)
#define APPSTR(s)                                                           \
    do {                                                                     \
        int _n = json_str((s), out + w, (w < (int)cap) ? cap - w : 0);       \
        w += _n;                                                             \
    } while(0)

    APP("{\"signature\":");
    APPSTR(map->protocol);
    APP(",\"nbits\":%u,\"n_bytes\":%u,\"modulation\":%u,\"user_confirmed\":%s,\"fields\":[",
        (unsigned)map->nbits, (unsigned)((map->nbits + 7) / 8), (unsigned)map->modulation,
        map->user_confirmed ? "true" : "false");
    for(size_t i = 0; i < map->n_fields; i++) {
        const ScField* fl = &map->fields[i];
        if(i) APP(",");
        APP("{\"name\":");
        APPSTR(fl->name);
        APP(",\"start_bit\":%u,\"length\":%u,\"class\":\"%s\",\"semantics\":",
            (unsigned)fl->start_bit, (unsigned)fl->length, sc_field_class_str(fl->cls));
        if(fl->semantics[0]) {
            APPSTR(fl->semantics);
        } else {
            APP("null");
        }
        APP("}");
    }
    APP("],\"checksum\":");
    if(map->has_checksum) {
        APP("{\"kind\":\"%s\",\"poly\":%u,\"init\":%u,\"gen\":%u,\"key\":%u,\"over_bytes\":%u}",
            sc_checksum_kind_str(map->checksum.kind), (unsigned)map->checksum.poly,
            (unsigned)map->checksum.init, (unsigned)map->checksum.gen, (unsigned)map->checksum.key,
            (unsigned)map->checksum_over_bytes);
    } else {
        APP("null");
    }
    APP(",\"confidence\":%.3f,\"reasoning\":", (double)confidence);
    {
        char reason[256];
        snprintf(reason, sizeof(reason),
                 "%u byte-segments; %d static, %d counter, %d slow; checksum=%s. "
                 "PROPOSAL - passive (no TX); user confirms before writing field_maps/.",
                 (unsigned)map->n_fields, count_class(map, SC_FIELD_STATIC),
                 count_class(map, SC_FIELD_COUNTER), count_class(map, SC_FIELD_SLOW),
                 map->has_checksum ? sc_checksum_kind_str(map->checksum.kind) : "none");
        APPSTR(reason);
    }
    APP("}");
#undef APP
#undef APPSTR
    if(w >= (int)cap) return -1; /* truncated */
    return w;
}
