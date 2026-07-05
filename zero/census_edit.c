#include "census_edit.h"

#include <furi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/core/sc_feature.h"
#include "../shared/core/sc_slice.h"
#include "../shared/core/sc_sub.h"
#include "census_storage.h"

#define EDIT_SUB_BUF      20480
#define EDIT_MAX_TIM      2048
#define EDIT_LOG_MAXBYTES 64

#define CENSUS_FIELDMAPS_DIR CENSUS_SIGNATURES_DIR "/field_maps"

size_t census_edit_load_sub(
    Storage* storage,
    const char* place_id,
    const char* sub_rel,
    uint8_t* out_frame,
    size_t cap_bytes,
    int32_t* out_unit_us,
    uint32_t* out_freq,
    char* out_preset,
    size_t preset_cap) {
    char abs[192];
    census_place_file(place_id, sub_rel, abs, sizeof(abs));

    char* text = malloc(EDIT_SUB_BUF);
    int32_t* timings = malloc(sizeof(int32_t) * EDIT_MAX_TIM);
    size_t nbits = 0;
    if(text && timings) {
        File* f = storage_file_alloc(storage);
        size_t tn = 0;
        ScSubMeta meta = {0};
        if(storage_file_open(f, abs, FSAM_READ, FSOM_OPEN_EXISTING)) {
            size_t rd = storage_file_read(f, text, EDIT_SUB_BUF - 1);
            text[rd] = '\0';
            sc_sub_parse(text, rd, &meta, timings, EDIT_MAX_TIM, &tn);
        }
        storage_file_close(f);
        storage_file_free(f);

        uint32_t freq = meta.frequency ? (uint32_t)meta.frequency : 0;
        ScModulation mod = SC_MOD_OOK;
        ScFeatureVector fv;
        sc_feature_compute(timings, tn, (int32_t)freq, mod, &fv);
        int32_t unit = fv.n_sym_dur > 0 ? fv.sym_dur_us[0] : 250;
        if(unit <= 0) unit = 250;
        nbits = sc_slice_bits(timings, tn, unit, out_frame, cap_bytes);
        if(out_unit_us) *out_unit_us = unit;
        if(out_freq) *out_freq = freq;
        if(out_preset) {
            strncpy(out_preset, meta.preset[0] ? meta.preset : "OOK650", preset_cap - 1);
            out_preset[preset_cap - 1] = '\0';
        }
    }
    if(text) free(text);
    if(timings) free(timings);
    return nbits;
}

/* Parse the sub_file (col 12) and freq (col 1) from a census_log row. */
static bool log_row_field(const char* line, int idx, char* dst, size_t cap) {
    const char* p = line;
    for(int i = 0; i < idx; i++) {
        while(*p && *p != ',' && *p != '\n')
            p++;
        if(*p != ',') return false;
        p++;
    }
    const char* e = p;
    while(*e && *e != ',' && *e != '\n' && *e != '\r')
        e++;
    size_t n = (size_t)(e - p);
    if(n >= cap) n = cap - 1;
    memcpy(dst, p, n);
    dst[n] = '\0';
    return true;
}

size_t census_edit_corpus(
    Storage* storage,
    const char* place_id,
    uint32_t freq,
    int32_t unit_us,
    uint8_t* frames,
    size_t max_frames,
    size_t stride_bytes,
    size_t* out_nbits) {
    char path[160];
    census_place_file(place_id, "census_log.csv", path, sizeof(path));
    File* f = storage_file_alloc(storage);
    size_t nf = 0;
    size_t min_bits = stride_bytes * 8;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[256];
        size_t li = 0;
        bool header = true;
        char c;
        while(nf < max_frames && storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 5) {
                    char fs[16], sub[80];
                    log_row_field(line, 1, fs, sizeof(fs));
                    log_row_field(line, 12, sub, sizeof(sub));
                    uint32_t rf = (uint32_t)strtoul(fs, NULL, 10);
                    /* same bin (within 1 kHz) and a real capture file */
                    uint32_t d = rf > freq ? rf - freq : freq - rf;
                    if(d <= 1000 && sub[0]) {
                        uint8_t* row = frames + nf * stride_bytes;
                        int32_t unit = unit_us;
                        uint32_t rowfreq = 0;
                        char preset[24];
                        size_t nb = census_edit_load_sub(
                            storage,
                            place_id,
                            sub,
                            row,
                            stride_bytes,
                            &unit,
                            &rowfreq,
                            preset,
                            sizeof(preset));
                        if(nb >= 8) {
                            if(nb < min_bits) min_bits = nb;
                            nf++;
                        }
                    }
                }
                header = false;
                li = 0;
            } else if(c != '\r') {
                line[li++] = c;
            }
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    if(out_nbits) *out_nbits = (nf > 0) ? min_bits : 0;
    return nf;
}

static void fieldmap_path(const char* protocol, char* out, size_t cap) {
    /* sanitize the protocol into a filename-safe stem */
    char stem[32];
    size_t j = 0;
    for(size_t i = 0; protocol[i] && j < sizeof(stem) - 1; i++) {
        char ch = protocol[i];
        stem[j++] =
            ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) ?
                ch :
                '_';
    }
    stem[j] = '\0';
    if(j == 0) {
        strncpy(stem, "unknown", sizeof(stem) - 1);
        stem[sizeof(stem) - 1] = '\0';
    }
    snprintf(out, cap, CENSUS_FIELDMAPS_DIR "/%s.fmap", stem);
}

bool census_fieldmap_load(Storage* storage, const char* protocol, ScFieldMap* out) {
    char path[160];
    fieldmap_path(protocol, path, sizeof(path));
    File* f = storage_file_alloc(storage);
    bool ok = false;
    char* buf = malloc(2048);
    if(buf && storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t rd = storage_file_read(f, buf, 2047);
        buf[rd] = '\0';
        ok = sc_fieldmap_parse(buf, rd, out);
    }
    storage_file_close(f);
    storage_file_free(f);
    if(buf) free(buf);
    return ok;
}

bool census_fieldmap_save(Storage* storage, const ScFieldMap* map) {
    storage_common_mkdir(storage, CENSUS_FIELDMAPS_DIR);
    char path[160];
    fieldmap_path(map->protocol, path, sizeof(path));
    char* buf = malloc(2048);
    if(!buf) return false;
    size_t len = sc_fieldmap_emit(map, buf, 2048);
    File* f = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(f, buf, len) == len;
    }
    storage_file_close(f);
    storage_file_free(f);
    free(buf);
    return ok;
}

void census_edit_log_tx(
    Storage* storage,
    const char* place_id,
    uint32_t freq,
    const char* preset,
    const uint8_t* frame,
    size_t nbits) {
    char path[160];
    census_place_file(place_id, "edits_log.csv", path, sizeof(path));
    /* create with header if missing */
    if(!storage_file_exists(storage, path)) {
        File* h = storage_file_alloc(storage);
        if(storage_file_open(h, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            const char* hdr = "ts_tick,freq_hz,preset,nbits,hexframe\n";
            storage_file_write(h, hdr, strlen(hdr));
        }
        storage_file_close(h);
        storage_file_free(h);
    }
    char hex[2 * EDIT_LOG_MAXBYTES + 1];
    size_t nbytes = (nbits + 7) / 8;
    if(nbytes > EDIT_LOG_MAXBYTES) nbytes = EDIT_LOG_MAXBYTES;
    for(size_t i = 0; i < nbytes; i++)
        snprintf(hex + i * 2, 3, "%02X", frame[i]);
    hex[nbytes * 2] = '\0';

    char row[220];
    snprintf(
        row,
        sizeof(row),
        "%lu,%lu,%s,%u,%s\n",
        (unsigned long)furi_get_tick(),
        (unsigned long)freq,
        preset ? preset : "",
        (unsigned)nbits,
        hex);
    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(f, row, strlen(row));
    }
    storage_file_close(f);
    storage_file_free(f);
}
