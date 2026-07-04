#include "esp_fingerprints.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "census_taxonomy.h"
#include "sc_types.h"

/* Copy field #idx (0-based, comma-separated) of `line` into dst[cap]. */
static bool fp_field(const char* line, int idx, char* dst, size_t cap) {
    const char* p = line;
    for(int i = 0; i < idx; i++) {
        while(*p && *p != ',' && *p != '\n') p++;
        if(*p != ',') return false;
        p++;
    }
    const char* e = p;
    while(*e && *e != ',' && *e != '\n' && *e != '\r') e++;
    size_t n = (size_t)(e - p);
    if(n >= cap) n = cap - 1;
    memcpy(dst, p, n);
    dst[n] = '\0';
    return true;
}

static ScCadenceClass cadence_from_str(const char* s) {
    if(strcmp(s, "periodic") == 0) return SC_CADENCE_PERIODIC;
    if(strcmp(s, "quasi-periodic") == 0) return SC_CADENCE_QUASI_PERIODIC;
    if(strcmp(s, "event-driven") == 0) return SC_CADENCE_EVENT_DRIVEN;
    if(strcmp(s, "near-continuous") == 0) return SC_CADENCE_NEAR_CONTINUOUS;
    if(strcmp(s, "seen-once") == 0) return SC_CADENCE_SEEN_ONCE;
    return SC_CADENCE_NONE;
}

bool esp_fingerprint_parse_line(
    const char* line, ScFingerprint* out, char* name_buf, size_t name_cap) {
    if(!line || !out) return false;
    /* id,freq_bin,modulation,sym_dur_us_1,sym_dur_us_2,sym_dur_us_3,n_symbols,est_bitrate,
       preamble_len,repeat_count,device_name,device_class,source,cadence_class,period_s,
       period_regularity,cadence_samples */
    char f[24];
    memset(out, 0, sizeof(*out));

    if(!fp_field(line, 1, f, sizeof(f))) return false;
    out->fv.freq_bin = (int32_t)strtol(f, NULL, 10);
    fp_field(line, 2, f, sizeof(f));
    out->fv.modulation = sc_modulation_from_str(f);

    int nsd = 0;
    for(int i = 0; i < 3; i++) {
        if(fp_field(line, 3 + i, f, sizeof(f)) && f[0]) {
            out->fv.sym_dur_us[i] = (int32_t)strtol(f, NULL, 10);
            nsd++;
        }
    }
    out->fv.n_sym_dur = nsd;
    fp_field(line, 6, f, sizeof(f));
    out->fv.n_symbols = (int32_t)strtol(f, NULL, 10);
    fp_field(line, 7, f, sizeof(f));
    out->fv.est_bitrate = (int32_t)strtol(f, NULL, 10);
    fp_field(line, 8, f, sizeof(f));
    out->fv.preamble_len = (int32_t)strtol(f, NULL, 10);
    fp_field(line, 9, f, sizeof(f));
    out->fv.repeat_count = (int32_t)strtol(f, NULL, 10);

    if(name_buf) {
        fp_field(line, 10, name_buf, name_cap);
        out->device_name = name_buf;
    }
    char cls[24];
    fp_field(line, 11, cls, sizeof(cls));
    out->device_class = census_class_from_id(cls);

    char cad[24];
    if(fp_field(line, 13, cad, sizeof(cad)) && cad[0]) {
        out->cadence_class = cadence_from_str(cad);
        if(fp_field(line, 14, f, sizeof(f)) && f[0]) out->period_s = (float)atof(f);
    } else {
        out->cadence_class = SC_CADENCE_NONE;
    }
    return true;
}

int esp_fingerprint_row(
    const char* id, const ScFeatureVector* fv, const char* device_name,
    const char* device_class, char* out, size_t cap) {
    char s1[12] = "", s2[12] = "", s3[12] = "";
    if(fv->n_sym_dur > 0) snprintf(s1, sizeof(s1), "%ld", (long)fv->sym_dur_us[0]);
    if(fv->n_sym_dur > 1) snprintf(s2, sizeof(s2), "%ld", (long)fv->sym_dur_us[1]);
    if(fv->n_sym_dur > 2) snprintf(s3, sizeof(s3), "%ld", (long)fv->sym_dur_us[2]);
    int n = snprintf(
        out, cap, "%s,%ld,%s,%s,%s,%s,%ld,%ld,%ld,%ld,%s,%s,user,,,,",
        id ? id : "", (long)fv->freq_bin, sc_modulation_str(fv->modulation), s1, s2, s3,
        (long)fv->n_symbols, (long)fv->est_bitrate, (long)fv->preamble_len,
        (long)fv->repeat_count, device_name ? device_name : "",
        device_class ? device_class : "");
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}
