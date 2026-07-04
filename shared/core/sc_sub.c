#include "sc_sub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- small helpers (const-correct, no strtok) --- */

static const char* line_end(const char* p, const char* end) {
    while(p < end && *p != '\n' && *p != '\r')
        p++;
    return p;
}

static const char* next_line(const char* p, const char* end) {
    p = line_end(p, end);
    while(p < end && (*p == '\n' || *p == '\r'))
        p++;
    return p;
}

/* Copy the value after "Key:" (trimmed) into dst[cap]. Returns 1 if key matched. */
static int match_kv(const char* line, const char* lend, const char* key, char* dst, size_t cap) {
    size_t klen = strlen(key);
    if((size_t)(lend - line) < klen) return 0;
    if(strncmp(line, key, klen) != 0) return 0;
    const char* v = line + klen;
    while(v < lend && (*v == ' ' || *v == '\t'))
        v++;
    const char* ve = lend;
    while(ve > v && (ve[-1] == ' ' || ve[-1] == '\t'))
        ve--;
    size_t n = (size_t)(ve - v);
    if(n >= cap) n = cap - 1;
    memcpy(dst, v, n);
    dst[n] = '\0';
    return 1;
}

ScResult sc_sub_parse(
    const char* text,
    size_t len,
    ScSubMeta* meta,
    int32_t* out_timings,
    size_t cap,
    size_t* out_n) {
    if(!text) return SC_ERR;

    ScSubMeta m = {0};
    size_t n = 0;
    int truncated = 0;
    char buf[80];

    const char* end = text + len;
    const char* p = text;
    while(p < end) {
        const char* lend = line_end(p, end);

        if(match_kv(p, lend, "Frequency:", buf, sizeof(buf))) {
            m.frequency = (int32_t)strtol(buf, NULL, 10);
        } else if(match_kv(p, lend, "Preset:", m.preset, sizeof(m.preset))) {
            /* stored */
        } else if(match_kv(p, lend, "Protocol:", m.protocol, sizeof(m.protocol))) {
            /* stored */
        } else if((size_t)(lend - p) >= 9 && strncmp(p, "RAW_Data:", 9) == 0) {
            const char* q = p + 9;
            while(q < lend) {
                while(q < lend && (*q == ' ' || *q == '\t'))
                    q++;
                if(q >= lend) break;
                char* qe = NULL;
                long v = strtol(q, &qe, 10);
                if(qe == q) break; /* no number consumed */
                if(out_timings && n < cap) {
                    out_timings[n] = (int32_t)v;
                } else if(n >= cap) {
                    truncated = 1;
                }
                n++;
                q = qe;
            }
        }
        p = next_line(p, end);
    }

    if(meta) *meta = m;
    if(out_n) *out_n = truncated ? cap : n;
    return truncated ? SC_TRUNCATED : SC_OK;
}

/* Append a formatted chunk to out; track offset; set overflow flag. */
static void append(char* out, size_t cap, size_t* off, int* overflow, const char* s) {
    size_t slen = strlen(s);
    if(*off + slen >= cap) {
        *overflow = 1;
        return;
    }
    memcpy(out + *off, s, slen);
    *off += slen;
}

static void append_int(char* out, size_t cap, size_t* off, int* overflow, long v) {
    char tmp[16];
    int k = snprintf(tmp, sizeof(tmp), "%ld", v);
    if(k < 0) {
        *overflow = 1;
        return;
    }
    append(out, cap, off, overflow, tmp);
}

ScResult sc_sub_encode(
    const ScSubMeta* meta,
    const int32_t* timings,
    size_t n,
    char* out,
    size_t cap,
    size_t values_per_line,
    size_t* out_len) {
    if(!out || cap == 0) return SC_ERR;
    if(values_per_line == 0) values_per_line = 512;

    size_t off = 0;
    int overflow = 0;
    const char* preset = (meta && meta->preset[0]) ? meta->preset :
                                                     "FuriHalSubGhzPresetOok650Async";
    const char* protocol = (meta && meta->protocol[0]) ? meta->protocol : "RAW";
    int32_t freq = meta ? meta->frequency : 0;

    append(out, cap, &off, &overflow, "Filetype: Flipper SubGhz RAW File\n");
    append(out, cap, &off, &overflow, "Version: 1\n");
    append(out, cap, &off, &overflow, "Frequency: ");
    append_int(out, cap, &off, &overflow, freq);
    append(out, cap, &off, &overflow, "\nPreset: ");
    append(out, cap, &off, &overflow, preset);
    append(out, cap, &off, &overflow, "\nProtocol: ");
    append(out, cap, &off, &overflow, protocol);
    append(out, cap, &off, &overflow, "\n");

    for(size_t i = 0; i < n;) {
        append(out, cap, &off, &overflow, "RAW_Data: ");
        size_t line_count = 0;
        for(; i < n && line_count < values_per_line; i++, line_count++) {
            if(line_count > 0) append(out, cap, &off, &overflow, " ");
            append_int(out, cap, &off, &overflow, (long)timings[i]);
        }
        append(out, cap, &off, &overflow, "\n");
    }

    if(overflow) {
        out[cap - 1] = '\0';
        if(out_len) *out_len = cap - 1;
        return SC_TRUNCATED;
    }
    out[off] = '\0';
    if(out_len) *out_len = off;
    return SC_OK;
}
