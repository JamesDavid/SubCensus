#include "census_brain.h"

#include <furi_hal_rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/core/sc_types.h"
#include "census_schema.h"
#include "census_storage.h"
#include "census_taxonomy.h"

/* Copy comma-field #idx of `line` into dst[cap]. */
static bool fp_field(const char* line, int idx, char* dst, size_t cap) {
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

static void fp_parse(const char* line, ScFingerprint* out, char* name, size_t name_cap) {
    char f[24];
    memset(out, 0, sizeof(*out));
    if(fp_field(line, 1, f, sizeof(f))) out->fv.freq_bin = (int32_t)strtol(f, NULL, 10);
    if(fp_field(line, 2, f, sizeof(f))) out->fv.modulation = sc_modulation_from_str(f);
    int nsd = 0;
    for(int i = 0; i < 3; i++) {
        if(fp_field(line, 3 + i, f, sizeof(f)) && f[0])
            out->fv.sym_dur_us[i] = (int32_t)strtol(f, NULL, 10), nsd++;
    }
    out->fv.n_sym_dur = nsd;
    if(fp_field(line, 6, f, sizeof(f))) out->fv.n_symbols = (int32_t)strtol(f, NULL, 10);
    if(fp_field(line, 7, f, sizeof(f))) out->fv.est_bitrate = (int32_t)strtol(f, NULL, 10);
    if(fp_field(line, 8, f, sizeof(f))) out->fv.preamble_len = (int32_t)strtol(f, NULL, 10);
    if(fp_field(line, 9, f, sizeof(f))) out->fv.repeat_count = (int32_t)strtol(f, NULL, 10);
    if(name) {
        fp_field(line, 10, name, name_cap);
        out->device_name = name;
    }
    char cls[24];
    fp_field(line, 11, cls, sizeof(cls));
    out->device_class = census_class_from_id(cls);
    out->cadence_class = SC_CADENCE_NONE;
}

void census_brain_load(Storage* storage, CensusBrain* brain) {
    brain->count = 0;
    File* f = storage_file_alloc(storage);
    if(storage_file_open(
           f, CENSUS_SIGNATURES_DIR "/fingerprints.csv", FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[256];
        size_t li = 0;
        bool header = true;
        char c;
        while(brain->count < CENSUS_BRAIN_MAX_FPS && storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 5) {
                    fp_parse(line, &brain->fps[brain->count], brain->names[brain->count], 24);
                    brain->count++;
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
}

bool census_brain_confirm_label(
    Storage* storage,
    const ScFeatureVector* fv,
    const char* device_class,
    const char* device_name) {
    char s1[12] = "", s2[12] = "", s3[12] = "";
    if(fv->n_sym_dur > 0) snprintf(s1, sizeof(s1), "%ld", (long)fv->sym_dur_us[0]);
    if(fv->n_sym_dur > 1) snprintf(s2, sizeof(s2), "%ld", (long)fv->sym_dur_us[1]);
    if(fv->n_sym_dur > 2) snprintf(s3, sizeof(s3), "%ld", (long)fv->sym_dur_us[2]);

    char row[224];
    snprintf(
        row,
        sizeof(row),
        "fp%08lx,%ld,%s,%s,%s,%s,%ld,%ld,%ld,%ld,%s,%s,user,,,,\n",
        (unsigned long)furi_hal_rtc_get_timestamp(),
        (long)fv->freq_bin,
        sc_modulation_str(fv->modulation),
        s1,
        s2,
        s3,
        (long)fv->n_symbols,
        (long)fv->est_bitrate,
        (long)fv->preamble_len,
        (long)fv->repeat_count,
        device_name ? device_name : "",
        device_class ? device_class : "");

    /* ensure the file exists with its header, then append (active-learning loop, System §6) */
    File* f = storage_file_alloc(storage);
    bool ok = false;
    if(!storage_file_exists(storage, CENSUS_SIGNATURES_DIR "/fingerprints.csv")) {
        if(storage_file_open(
               f, CENSUS_SIGNATURES_DIR "/fingerprints.csv", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_write(f, FINGERPRINTS_HEADER "\n", strlen(FINGERPRINTS_HEADER) + 1);
        }
        storage_file_close(f);
    }
    if(storage_file_open(
           f, CENSUS_SIGNATURES_DIR "/fingerprints.csv", FSAM_WRITE, FSOM_OPEN_APPEND)) {
        ok = storage_file_write(f, row, strlen(row)) == strlen(row);
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}
