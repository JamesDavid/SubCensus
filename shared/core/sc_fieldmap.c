#include "sc_fieldmap.h"

#include <stdlib.h>
#include <string.h>

const char* sc_field_class_str(uint8_t cls) {
    switch(cls) {
    case SC_FIELD_STATIC:
        return "static";
    case SC_FIELD_SLOW:
        return "slow";
    case SC_FIELD_COUNTER:
        return "counter";
    case SC_FIELD_CHECKSUM:
        return "checksum";
    default:
        return "data";
    }
}

uint8_t sc_field_class_from_str(const char* s) {
    if(!s) return SC_FIELD_DATA;
    if(strcmp(s, "static") == 0) return SC_FIELD_STATIC;
    if(strcmp(s, "slow") == 0) return SC_FIELD_SLOW;
    if(strcmp(s, "counter") == 0) return SC_FIELD_COUNTER;
    if(strcmp(s, "checksum") == 0) return SC_FIELD_CHECKSUM;
    return SC_FIELD_DATA;
}

uint32_t sc_field_get(const uint8_t* frame, size_t nbytes, size_t start_bit, size_t length) {
    uint32_t v = 0;
    if(length > 32) length = 32;
    for(size_t i = 0; i < length; i++) {
        size_t bit = start_bit + i;
        size_t byte = bit / 8;
        int b = 0;
        if(byte < nbytes) b = (frame[byte] >> (7 - (bit % 8))) & 1;
        v = (v << 1) | (uint32_t)b;
    }
    return v;
}

void sc_field_set(uint8_t* frame, size_t nbytes, size_t start_bit, size_t length, uint32_t value) {
    if(length > 32) length = 32;
    for(size_t i = 0; i < length; i++) {
        size_t bit = start_bit + i;
        size_t byte = bit / 8;
        if(byte >= nbytes) continue;
        int b = (value >> (length - 1 - i)) & 1;
        uint8_t mask = (uint8_t)(0x80u >> (bit % 8));
        if(b)
            frame[byte] |= mask;
        else
            frame[byte] &= (uint8_t)~mask;
    }
}

uint8_t sc_checksum_compute(const ScChecksumSpec* spec, const uint8_t* data, size_t n) {
    if(!spec) return 0;
    switch(spec->kind) {
    case SC_CK_XOR:
        return sc_xor_bytes(data, n);
    case SC_CK_SUM:
        return sc_add_bytes(data, n);
    case SC_CK_CRC8:
        return sc_crc8(data, n, spec->poly, spec->init);
    case SC_CK_CRC8LE:
        return sc_crc8le(data, n, spec->poly, spec->init);
    case SC_CK_LFSR8:
        return sc_lfsr_digest8(data, n, spec->gen, spec->key);
    default:
        return 0;
    }
}

bool sc_fieldmap_resign(const ScFieldMap* map, uint8_t* frame, size_t nbytes) {
    if(!map || !map->has_checksum) return false;
    /* find the checksum field */
    const ScField* ck = NULL;
    for(size_t i = 0; i < map->n_fields; i++) {
        if(map->fields[i].cls == SC_FIELD_CHECKSUM) {
            ck = &map->fields[i];
            break;
        }
    }
    if(!ck) return false;
    size_t over = map->checksum_over_bytes;
    if(over > nbytes) over = nbytes;
    uint8_t sum = sc_checksum_compute(&map->checksum, frame, over);
    sc_field_set(frame, nbytes, ck->start_bit, ck->length, sum);
    return true;
}

/* --- tiny tokenizer for the .fmap text format --- */

/* Copy the next whitespace-delimited token from *p into tok[cap]; advance *p. Returns false at
 * end-of-line/string. With `decode`, '_' becomes space and a lone "-" becomes empty (value
 * tokens); keys/magic/numbers are read raw (decode=false) so an embedded '_' stays literal. */
static bool fm_next_token(const char** p, const char* end, char* tok, size_t cap, bool decode) {
    const char* s = *p;
    while(s < end && (*s == ' ' || *s == '\t')) s++;
    if(s >= end || *s == '\n' || *s == '\r') {
        *p = s;
        return false;
    }
    size_t j = 0;
    while(s < end && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') {
        char c = *s++;
        if(decode && c == '_') c = ' ';
        if(j < cap - 1) tok[j++] = c;
    }
    tok[j] = '\0';
    if(decode && strcmp(tok, "-") == 0) tok[0] = '\0';
    *p = s;
    return true;
}

static long fm_tok_long(const char** p, const char* end) {
    char t[24];
    if(!fm_next_token(p, end, t, sizeof(t), false)) return 0;
    return (long)strtol(t, NULL, 0);
}

static void fm_skip_line(const char** p, const char* end) {
    const char* s = *p;
    while(s < end && *s != '\n')
        s++;
    if(s < end) s++;
    *p = s;
}

bool sc_fieldmap_parse(const char* text, size_t len, ScFieldMap* out) {
    if(!text || !out) return false;
    memset(out, 0, sizeof(*out));
    const char* p = text;
    const char* end = text + len;
    char key[24];
    bool saw_header = false;
    while(p < end) {
        const char* line = p;
        if(!fm_next_token(&p, end, key, sizeof(key), false)) {
            fm_skip_line(&p, end);
            continue;
        }
        if(strcmp(key, "SC_FIELDMAP") == 0) {
            saw_header = true;
        } else if(strcmp(key, "protocol") == 0) {
            fm_next_token(&p, end, out->protocol, sizeof(out->protocol), true);
        } else if(strcmp(key, "modulation") == 0) {
            out->modulation = (uint8_t)fm_tok_long(&p, end);
        } else if(strcmp(key, "nbits") == 0) {
            out->nbits = (uint16_t)fm_tok_long(&p, end);
        } else if(strcmp(key, "field") == 0) {
            if(out->n_fields < SC_FIELDMAP_MAX_FIELDS) {
                ScField* fl = &out->fields[out->n_fields];
                memset(fl, 0, sizeof(*fl));
                fm_next_token(&p, end, fl->name, sizeof(fl->name), true);
                fl->start_bit = (uint16_t)fm_tok_long(&p, end);
                fl->length = (uint16_t)fm_tok_long(&p, end);
                char clsn[16];
                fm_next_token(&p, end, clsn, sizeof(clsn), false);
                fl->cls = sc_field_class_from_str(clsn);
                fm_next_token(&p, end, fl->semantics, sizeof(fl->semantics), true);
                out->n_fields++;
            }
        } else if(strcmp(key, "checksum") == 0) {
            out->has_checksum = true;
            out->checksum.kind = (ScChecksumKind)fm_tok_long(&p, end);
            out->checksum.poly = (uint8_t)fm_tok_long(&p, end);
            out->checksum.init = (uint8_t)fm_tok_long(&p, end);
            out->checksum.gen = (uint8_t)fm_tok_long(&p, end);
            out->checksum.key = (uint8_t)fm_tok_long(&p, end);
            out->checksum_over_bytes = (uint16_t)fm_tok_long(&p, end);
        } else if(strcmp(key, "source") == 0) {
            char src[16];
            fm_next_token(&p, end, src, sizeof(src), false);
            out->user_confirmed = (strcmp(src, "user") == 0);
        }
        (void)line;
        fm_skip_line(&p, end);
    }
    return saw_header && out->nbits > 0;
}

/* Append a token to out, encoding spaces as '_' and empty as "-". */
static size_t fm_emit_tok(char* out, size_t cap, size_t off, const char* s, bool trailing_space) {
    if(!s || s[0] == '\0') {
        if(off < cap - 1) out[off++] = '-';
    } else {
        for(size_t i = 0; s[i] && off < cap - 1; i++) out[off++] = (s[i] == ' ') ? '_' : s[i];
    }
    if(trailing_space && off < cap - 1) out[off++] = ' ';
    return off;
}

static size_t fm_emit_num(char* out, size_t cap, size_t off, long v, bool trailing_space) {
    char t[16];
    int n = 0;
    /* itoa (base 10, non-negative expected) */
    char tmp[16];
    int m = 0;
    unsigned long uv = (v < 0) ? (unsigned long)(-v) : (unsigned long)v;
    if(v < 0 && off < cap - 1) out[off++] = '-';
    do {
        tmp[m++] = (char)('0' + (uv % 10));
        uv /= 10;
    } while(uv && m < 15);
    while(m > 0)
        t[n++] = tmp[--m];
    for(int i = 0; i < n && off < cap - 1; i++)
        out[off++] = t[i];
    if(trailing_space && off < cap - 1) out[off++] = ' ';
    return off;
}

size_t sc_fieldmap_emit(const ScFieldMap* map, char* out, size_t cap) {
    if(!map || !out || cap == 0) return 0;
    size_t off = 0;
#define PUTS(s)                                    \
    for(const char* q = (s); *q && off < cap - 1;) \
    out[off++] = *q++
    PUTS("SC_FIELDMAP v1\n");
    PUTS("protocol ");
    off = fm_emit_tok(out, cap, off, map->protocol, false);
    PUTS("\n");
    PUTS("modulation ");
    off = fm_emit_num(out, cap, off, map->modulation, false);
    PUTS("\n");
    PUTS("nbits ");
    off = fm_emit_num(out, cap, off, map->nbits, false);
    PUTS("\n");
    for(size_t i = 0; i < map->n_fields; i++) {
        const ScField* fl = &map->fields[i];
        PUTS("field ");
        off = fm_emit_tok(out, cap, off, fl->name, true);
        off = fm_emit_num(out, cap, off, fl->start_bit, true);
        off = fm_emit_num(out, cap, off, fl->length, true);
        off = fm_emit_tok(out, cap, off, sc_field_class_str(fl->cls), true);
        off = fm_emit_tok(out, cap, off, fl->semantics, false);
        PUTS("\n");
    }
    if(map->has_checksum) {
        PUTS("checksum ");
        off = fm_emit_num(out, cap, off, map->checksum.kind, true);
        off = fm_emit_num(out, cap, off, map->checksum.poly, true);
        off = fm_emit_num(out, cap, off, map->checksum.init, true);
        off = fm_emit_num(out, cap, off, map->checksum.gen, true);
        off = fm_emit_num(out, cap, off, map->checksum.key, true);
        off = fm_emit_num(out, cap, off, map->checksum_over_bytes, false);
        PUTS("\n");
    }
    PUTS("source ");
    PUTS(map->user_confirmed ? "user" : "proposed");
    PUTS("\n");
#undef PUTS
    out[off < cap ? off : cap - 1] = '\0';
    return off;
}

void sc_fieldmap_from_diff(
    const ScBitProfile* profiles,
    size_t nbits,
    const char* protocol,
    uint8_t modulation,
    ScFieldMap* out) {
    memset(out, 0, sizeof(*out));
    if(protocol) {
        strncpy(out->protocol, protocol, sizeof(out->protocol) - 1);
        out->protocol[sizeof(out->protocol) - 1] = '\0';
    }
    out->modulation = modulation;
    out->nbits = (uint16_t)nbits;
    out->user_confirmed = false;

    /* Coalesce byte-granular segments by dominant class (matches the Pi's byte segmentation:
     * all-static byte => static; any counter bit => counter; else slow). */
    size_t nbytes = (nbits + 7) / 8;
    for(size_t b = 0; b < nbytes && out->n_fields < SC_FIELDMAP_MAX_FIELDS; b++) {
        size_t base = b * 8;
        int has_counter = 0, all_static = 1;
        for(size_t k = 0; k < 8 && base + k < nbits; k++) {
            uint8_t c = profiles[base + k].cls;
            if(c == SC_BIT_COUNTER) has_counter = 1;
            if(c != SC_BIT_STATIC) all_static = 0;
        }
        uint8_t cls = all_static ? SC_FIELD_STATIC : (has_counter ? SC_FIELD_COUNTER : SC_FIELD_SLOW);
        ScField* fl = &out->fields[out->n_fields++];
        memset(fl, 0, sizeof(*fl));
        /* name: byte0, byte1, ... */
        fl->name[0] = 'b';
        fl->name[1] = 'y';
        fl->name[2] = 't';
        fl->name[3] = 'e';
        size_t nb = b, m = 4;
        char tmp[8];
        int t = 0;
        do {
            tmp[t++] = (char)('0' + (nb % 10));
            nb /= 10;
        } while(nb && t < 7);
        while(t > 0 && m < SC_FIELD_NAME_LEN - 1)
            fl->name[m++] = tmp[--t];
        fl->name[m] = '\0';
        fl->start_bit = (uint16_t)base;
        fl->length = (uint16_t)((base + 8 <= nbits) ? 8 : (nbits - base));
        fl->cls = cls;
    }
}
