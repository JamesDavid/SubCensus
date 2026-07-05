#include "census_storage.h"

#include <flipper_format/flipper_format.h>
#include <furi_hal_rtc.h>
#include <stdio.h>
#include <string.h>

#include "census_schema.h"

#define SETTINGS_HEADER   "SubCensusZero Settings"
#define SETTINGS_VERSION  1
#define PLACE_META_HEADER "SubCensusZero Place"

void census_settings_set_defaults(CensusSettings* s) {
    memset(s, 0, sizeof(*s));
    strncpy(s->place_id, "home", CENSUS_PLACE_ID_LEN - 1);
    s->mode = CensusModeCamp;
    s->freq_preset = 0; /* US */
    s->use_watchlist = true;
    s->rssi_auto = true;
    s->rssi_threshold = -80;
    s->capture_preset = CensusCaptureOok650;
    s->dwell_ms = 80;
    s->capture_max_ms = 1500;
    s->signal_end_gap_ms = 120;
    s->min_gap_ms = 500;
    s->survey_minutes = 15;
    s->recon_grid = CensusReconGridHybrid;
    s->recon_step_hz = 250000; /* coarse background grid step / RX BW (§3.3 Stage A) */
    s->camp_freq_hz = 433920000; /* explicit default; 0 would mean Auto=busiest */
    s->auto_classify = true;
    s->match_db = true;
    s->notify = CensusNotifyLed;
    s->custom_count = 0;
}

bool census_settings_save(Storage* storage, const CensusSettings* s) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = flipper_format_file_open_always(ff, CENSUS_CONFIG_PATH);
    if(ok) ok = flipper_format_write_header_cstr(ff, SETTINGS_HEADER, SETTINGS_VERSION);
    if(ok) ok = flipper_format_write_string_cstr(ff, "place_id", s->place_id);
    uint32_t u;
    int32_t i;
#define WU(key, val)     \
    u = (uint32_t)(val); \
    if(ok) ok = flipper_format_write_uint32(ff, key, &u, 1)
#define WI(key, val)    \
    i = (int32_t)(val); \
    if(ok) ok = flipper_format_write_int32(ff, key, &i, 1)
    WU("mode", s->mode);
    WU("freq_preset", s->freq_preset);
    WU("use_watchlist", s->use_watchlist);
    WU("rssi_auto", s->rssi_auto);
    WI("rssi_threshold", s->rssi_threshold);
    WU("capture_preset", s->capture_preset);
    WU("dwell_ms", s->dwell_ms);
    WU("capture_max_ms", s->capture_max_ms);
    WU("signal_end_gap_ms", s->signal_end_gap_ms);
    WU("min_gap_ms", s->min_gap_ms);
    WU("survey_minutes", s->survey_minutes);
    WU("recon_grid", s->recon_grid);
    WU("recon_step_hz", s->recon_step_hz);
    WU("camp_freq_hz", s->camp_freq_hz);
    WU("auto_classify", s->auto_classify);
    WU("match_db", s->match_db);
    WU("notify", s->notify);
    WU("custom_count", s->custom_count);
    if(ok && s->custom_count > 0)
        ok = flipper_format_write_uint32(ff, "custom_freqs", s->custom_freqs, s->custom_count);
#undef WU
#undef WI
    flipper_format_free(ff);
    return ok;
}

bool census_settings_load(Storage* storage, CensusSettings* s) {
    census_settings_set_defaults(s);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(!flipper_format_file_open_existing(ff, CENSUS_CONFIG_PATH)) {
        flipper_format_free(ff);
        return false; /* no config yet — defaults stand */
    }
    FuriString* tmp = furi_string_alloc();
    uint32_t version = 0;
    bool ok = flipper_format_read_header(ff, tmp, &version);
    if(ok && flipper_format_read_string(ff, "place_id", tmp)) {
        strncpy(s->place_id, furi_string_get_cstr(tmp), CENSUS_PLACE_ID_LEN - 1);
        s->place_id[CENSUS_PLACE_ID_LEN - 1] = '\0';
    }
    uint32_t u;
    int32_t i;
#define RU(key, field) \
    if(flipper_format_read_uint32(ff, key, &u, 1)) field = u
#define RI(key, field) \
    if(flipper_format_read_int32(ff, key, &i, 1)) field = i
    RU("mode", s->mode);
    RU("freq_preset", s->freq_preset);
    RU("use_watchlist", s->use_watchlist);
    RU("rssi_auto", s->rssi_auto);
    RI("rssi_threshold", s->rssi_threshold);
    RU("capture_preset", s->capture_preset);
    RU("dwell_ms", s->dwell_ms);
    RU("capture_max_ms", s->capture_max_ms);
    RU("signal_end_gap_ms", s->signal_end_gap_ms);
    RU("min_gap_ms", s->min_gap_ms);
    RU("survey_minutes", s->survey_minutes);
    RU("recon_grid", s->recon_grid);
    RU("recon_step_hz", s->recon_step_hz);
    RU("camp_freq_hz", s->camp_freq_hz);
    RU("auto_classify", s->auto_classify);
    RU("match_db", s->match_db);
    RU("notify", s->notify);
    RU("custom_count", s->custom_count);
    if(s->custom_count > CENSUS_CUSTOM_MAX) s->custom_count = CENSUS_CUSTOM_MAX;
    if(s->custom_count > 0)
        flipper_format_read_uint32(ff, "custom_freqs", s->custom_freqs, s->custom_count);
#undef RU
#undef RI
    furi_string_free(tmp);
    flipper_format_free(ff);
    return ok;
}

/* --- places --- */

void census_place_dir(const char* place_id, char* out, size_t cap) {
    snprintf(out, cap, CENSUS_PLACES_DIR "/%s", place_id);
}

void census_place_file(const char* place_id, const char* filename, char* out, size_t cap) {
    snprintf(out, cap, CENSUS_PLACES_DIR "/%s/%s", place_id, filename);
}

void census_place_id_from_name(const char* name, char* out_id, size_t cap) {
    /* slugify: lowercase, [a-z0-9] kept, others -> '-', collapse repeats, trim */
    char slug[CENSUS_PLACE_ID_LEN];
    size_t j = 0;
    bool prev_dash = false;
    for(size_t k = 0; name[k] && j < sizeof(slug) - 6; k++) {
        char c = name[k];
        if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if(alnum) {
            slug[j++] = c;
            prev_dash = false;
        } else if(!prev_dash && j > 0) {
            slug[j++] = '-';
            prev_dash = true;
        }
    }
    while(j > 0 && slug[j - 1] == '-')
        j--;
    slug[j] = '\0';
    if(j == 0) {
        strncpy(slug, "place", sizeof(slug) - 6);
        slug[sizeof(slug) - 6] = '\0';
    }
    /* short djb2 hash of the original name for uniqueness/rename-safety (Zero §4) */
    uint32_t h = 5381;
    for(size_t k = 0; name[k]; k++)
        h = ((h << 5) + h) + (uint8_t)name[k];
    snprintf(out_id, cap, "%s_%04x", slug, (unsigned)(h & 0xFFFF));
}

static bool census_write_csv_header(Storage* storage, const char* path, const char* header) {
    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        size_t len = strlen(header);
        ok = storage_file_write(f, header, len) == len;
        ok = ok && storage_file_write(f, "\n", 1) == 1;
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

static bool census_place_write_meta(
    Storage* storage,
    const char* place_id,
    const char* name,
    const char* location) {
    char path[128];
    census_place_file(place_id, "place.meta", path, sizeof(path));
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = flipper_format_file_open_always(ff, path);
    if(ok) ok = flipper_format_write_header_cstr(ff, PLACE_META_HEADER, 1);
    if(ok) ok = flipper_format_write_string_cstr(ff, "name", name);
    uint32_t created = furi_hal_rtc_get_timestamp();
    if(ok) ok = flipper_format_write_uint32(ff, "created", &created, 1);
    if(ok) ok = flipper_format_write_string_cstr(ff, "notes", "");
    /* optional location tag: manual text, or lat/lon if an external GPS is wired (§5.6) */
    if(ok) ok = flipper_format_write_string_cstr(ff, "location", location ? location : "");
    flipper_format_free(ff);
    return ok;
}

bool census_place_create(Storage* storage, const char* name, char* out_id, size_t cap) {
    char id[CENSUS_PLACE_ID_LEN];
    census_place_id_from_name(name, id, sizeof(id));

    char dir[128];
    census_place_dir(id, dir, sizeof(dir));
    if(storage_dir_exists(storage, dir)) {
        if(out_id) strncpy(out_id, id, cap - 1), out_id[cap - 1] = '\0';
        return false; /* already exists */
    }
    FS_Error e = storage_common_mkdir(storage, dir);
    if(e != FSE_OK && e != FSE_EXIST) return false;

    char captures[160];
    census_place_file(id, "captures", captures, sizeof(captures));
    storage_common_mkdir(storage, captures);

    bool ok = census_place_write_meta(storage, id, name, "");
    char path[160];
    census_place_file(id, "occupancy.csv", path, sizeof(path));
    ok = census_write_csv_header(storage, path, OCCUPANCY_HEADER) && ok;
    census_place_file(id, "watchlist.csv", path, sizeof(path));
    ok = census_write_csv_header(storage, path, WATCHLIST_HEADER) && ok;
    census_place_file(id, "census_log.csv", path, sizeof(path));
    ok = census_write_csv_header(storage, path, CENSUS_LOG_HEADER) && ok;

    if(out_id) {
        strncpy(out_id, id, cap - 1);
        out_id[cap - 1] = '\0';
    }
    return ok;
}

size_t census_place_list(Storage* storage, char ids[][CENSUS_PLACE_ID_LEN], size_t max) {
    File* dir = storage_file_alloc(storage);
    size_t count = 0;
    if(storage_dir_open(dir, CENSUS_PLACES_DIR)) {
        FileInfo info;
        char name[CENSUS_PLACE_ID_LEN];
        while(count < max && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) {
                strncpy(ids[count], name, CENSUS_PLACE_ID_LEN - 1);
                ids[count][CENSUS_PLACE_ID_LEN - 1] = '\0';
                count++;
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    return count;
}

bool census_place_name(Storage* storage, const char* place_id, char* out, size_t cap) {
    char path[128];
    census_place_file(place_id, "place.meta", path, sizeof(path));
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    if(flipper_format_file_open_existing(ff, path)) {
        FuriString* tmp = furi_string_alloc();
        uint32_t version = 0;
        if(flipper_format_read_header(ff, tmp, &version) &&
           flipper_format_read_string(ff, "name", tmp)) {
            strncpy(out, furi_string_get_cstr(tmp), cap - 1);
            out[cap - 1] = '\0';
            ok = true;
        }
        furi_string_free(tmp);
    }
    flipper_format_free(ff);
    if(!ok) {
        strncpy(out, place_id, cap - 1); /* fall back to id */
        out[cap - 1] = '\0';
    }
    return ok;
}

/* Read a string field from place.meta (name / location); falls back to `fallback`. */
static void census_place_meta_str(
    Storage* storage,
    const char* place_id,
    const char* key,
    char* out,
    size_t cap,
    const char* fallback) {
    char path[128];
    census_place_file(place_id, "place.meta", path, sizeof(path));
    strncpy(out, fallback, cap - 1);
    out[cap - 1] = '\0';
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_existing(ff, path)) {
        FuriString* tmp = furi_string_alloc();
        uint32_t version = 0;
        if(flipper_format_read_header(ff, tmp, &version) &&
           flipper_format_read_string(ff, key, tmp)) {
            strncpy(out, furi_string_get_cstr(tmp), cap - 1);
            out[cap - 1] = '\0';
        }
        furi_string_free(tmp);
    }
    flipper_format_free(ff);
}

/* Rewrite place.meta preserving created, with a new name and/or location (id is rename-safe). */
static bool census_place_rewrite_meta(
    Storage* storage,
    const char* place_id,
    const char* name,
    const char* location) {
    char path[128];
    census_place_file(place_id, "place.meta", path, sizeof(path));
    uint32_t created = furi_hal_rtc_get_timestamp();
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_existing(ff, path)) {
        FuriString* tmp = furi_string_alloc();
        uint32_t version = 0;
        flipper_format_read_header(ff, tmp, &version);
        flipper_format_read_uint32(ff, "created", &created, 1);
        furi_string_free(tmp);
    }
    flipper_format_free(ff);

    ff = flipper_format_file_alloc(storage);
    bool ok = flipper_format_file_open_always(ff, path);
    if(ok) ok = flipper_format_write_header_cstr(ff, PLACE_META_HEADER, 1);
    if(ok) ok = flipper_format_write_string_cstr(ff, "name", name);
    if(ok) ok = flipper_format_write_uint32(ff, "created", &created, 1);
    if(ok) ok = flipper_format_write_string_cstr(ff, "notes", "");
    if(ok) ok = flipper_format_write_string_cstr(ff, "location", location);
    flipper_format_free(ff);
    return ok;
}

bool census_place_rename(Storage* storage, const char* place_id, const char* new_name) {
    char loc[CENSUS_PLACE_NAME_LEN];
    census_place_meta_str(storage, place_id, "location", loc, sizeof(loc), "");
    return census_place_rewrite_meta(storage, place_id, new_name, loc);
}

void census_place_location(Storage* storage, const char* place_id, char* out, size_t cap) {
    census_place_meta_str(storage, place_id, "location", out, cap, "");
}

bool census_place_set_location(Storage* storage, const char* place_id, const char* location) {
    char name[CENSUS_PLACE_NAME_LEN];
    census_place_meta_str(storage, place_id, "name", name, sizeof(name), place_id);
    return census_place_rewrite_meta(storage, place_id, name, location);
}

bool census_place_delete(Storage* storage, const char* place_id) {
    char dir[128];
    census_place_dir(place_id, dir, sizeof(dir));
    return storage_simply_remove_recursive(storage, dir);
}

/* Read the `source` (column 4) of a watchlist line into src[cap]. */
static void watchlist_row_source(const char* line, char* src, size_t cap) {
    const char* p = line;
    for(int col = 0; col < 4 && *p; col++) {
        while(*p && *p != ',')
            p++;
        if(*p == ',') p++;
    }
    size_t j = 0;
    while(*p && *p != ',' && *p != '\n' && *p != '\r' && j < cap - 1)
        src[j++] = *p++;
    src[j] = '\0';
}

size_t census_watchlist_freqs(Storage* storage, const char* place_id, uint32_t* out, size_t cap) {
    char path[160];
    census_place_file(place_id, "watchlist.csv", path, sizeof(path));
    File* f = storage_file_alloc(storage);
    size_t n = 0;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[128];
        size_t li = 0;
        bool header = true;
        char c;
        while(n < cap && storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 0) {
                    char src[16];
                    watchlist_row_source(line, src, sizeof(src));
                    /* excluded entries are omitted from monitoring (§6 / System §9) */
                    if(strcmp(src, "user-exclude") != 0)
                        out[n++] = (uint32_t)strtoul(line, NULL, 10); /* col 0 = freq_hz */
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
    return n;
}

/* Parse a watchlist line "freq,mod,threshold,occ,source" -> freq, threshold, source. */
static void
    watchlist_row_parse(const char* line, uint32_t* freq, float* thr, char* src, size_t scap) {
    char* p = (char*)line;
    *freq = (uint32_t)strtoul(p, &p, 10);
    if(*p == ',') p++;
    while(*p && *p != ',') /* skip modulation (col 1) */
        p++;
    if(*p == ',') p++;
    *thr = strtof(p, &p); /* threshold_dbm (col 2) */
    watchlist_row_source(line, src, scap);
}

size_t census_watchlist_load(
    Storage* storage,
    const char* place_id,
    uint32_t* out_freqs,
    float* out_thr,
    size_t cap) {
    char path[160];
    census_place_file(place_id, "watchlist.csv", path, sizeof(path));
    File* f = storage_file_alloc(storage);
    size_t n = 0;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[128];
        size_t li = 0;
        bool header = true;
        char c;
        while(n < cap && storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 0) {
                    uint32_t freq;
                    float thr;
                    char src[16];
                    watchlist_row_parse(line, &freq, &thr, src, sizeof(src));
                    if(strcmp(src, "user-exclude") != 0) {
                        out_freqs[n] = freq;
                        if(out_thr) out_thr[n] = thr;
                        n++;
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
    return n;
}

bool census_watchlist_threshold(
    Storage* storage,
    const char* place_id,
    uint32_t freq,
    float* out_thr) {
    uint32_t freqs[64];
    float thr[64];
    size_t n = census_watchlist_load(storage, place_id, freqs, thr, 64);
    for(size_t i = 0; i < n; i++) {
        if(freqs[i] == freq) {
            if(out_thr) *out_thr = thr[i];
            return true;
        }
    }
    return false;
}

bool census_watchlist_set_source(
    Storage* storage,
    const char* place_id,
    uint32_t freq,
    const char* source) {
    char path[160];
    census_place_file(place_id, "watchlist.csv", path, sizeof(path));

    /* read all rows into a buffer, updating the matching freq's source (or note it's missing) */
    char* buf = malloc(4096);
    if(!buf) return false;
    size_t out = 0;
    bool found = false;
    out += (size_t)snprintf(buf + out, 4096 - out, "%s\n", WATCHLIST_HEADER);

    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[160];
        size_t li = 0;
        bool header = true;
        char c;
        while(storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 0 && out < 4000) {
                    uint32_t rf = (uint32_t)strtoul(line, NULL, 10);
                    if(rf == freq) {
                        found = true;
                        /* rewrite this row keeping freq/mod/thr/occ, swapping source */
                        char* p = line;
                        char cols[4][24] = {{0}};
                        for(int col = 0; col < 4; col++) {
                            size_t j = 0;
                            while(*p && *p != ',' && j < 23)
                                cols[col][j++] = *p++;
                            cols[col][j] = '\0';
                            if(*p == ',') p++;
                        }
                        out += (size_t)snprintf(
                            buf + out,
                            4096 - out,
                            "%s,%s,%s,%s,%s\n",
                            cols[0],
                            cols[1][0] ? cols[1] : "OOK",
                            cols[2][0] ? cols[2] : "-80.0",
                            cols[3][0] ? cols[3] : "0.0000",
                            source);
                    } else {
                        out += (size_t)snprintf(buf + out, 4096 - out, "%s\n", line);
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

    if(!found && out < 3900) {
        /* pinned/excluded a freq not in the watchlist -> add it (user-pin/exclude, §9) */
        out += (size_t)snprintf(
            buf + out, 4096 - out, "%lu,OOK,-80.0,0.0000,%s\n", (unsigned long)freq, source);
    }

    File* w = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(w, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(w, buf, out) == out;
    }
    storage_file_close(w);
    storage_file_free(w);
    free(buf);
    return ok;
}

/* Copy comma-field #idx of `line` (up to a newline) into dst[cap]. */
static void csv_field(const char* line, int idx, char* dst, size_t cap) {
    const char* p = line;
    for(int i = 0; i < idx; i++) {
        while(*p && *p != ',' && *p != '\n')
            p++;
        if(*p == ',') p++;
    }
    const char* e = p;
    while(*e && *e != ',' && *e != '\n' && *e != '\r')
        e++;
    size_t n = (size_t)(e - p);
    if(n >= cap) n = cap - 1;
    memcpy(dst, p, n);
    dst[n] = '\0';
}

bool census_log_set_label(
    Storage* storage,
    const char* place_id,
    const char* sub_file,
    const char* label) {
    char path[160], tmp[176];
    census_place_file(place_id, "census_log.csv", path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    File* in = storage_file_alloc(storage);
    File* out = storage_file_alloc(storage);
    bool updated = false;
    bool ok_open = storage_file_open(in, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                   storage_file_open(out, tmp, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok_open) {
        char line[256];
        size_t li = 0;
        bool header = true;
        char c;
        for(;;) {
            int got = storage_file_read(in, &c, 1);
            bool eol = (got == 1 && (c == '\n')) || li >= sizeof(line) - 1;
            if(got == 1 && !eol) {
                if(c != '\r') line[li++] = c;
                continue;
            }
            line[li] = '\0';
            if(li > 0) {
                if(header) {
                    storage_file_write(out, line, strlen(line));
                    storage_file_write(out, "\n", 1);
                    header = false;
                } else {
                    /* census_log: sub_file is col 12, label is col 13 (last) */
                    char sub[80];
                    csv_field(line, 12, sub, sizeof(sub));
                    char row[288];
                    if(strcmp(sub, sub_file) == 0) {
                        /* rebuild the row keeping cols 0..12, replacing col 13 with label */
                        size_t w = 0;
                        const char* p = line;
                        int col = 0;
                        while(col < 13) {
                            char fld[80];
                            csv_field(line, col, fld, sizeof(fld));
                            w += (size_t)snprintf(
                                row + w, sizeof(row) - w, "%s%s", col ? "," : "", fld);
                            col++;
                            (void)p;
                        }
                        w += (size_t)snprintf(row + w, sizeof(row) - w, ",%s\n", label);
                        storage_file_write(out, row, w);
                        updated = true;
                    } else {
                        storage_file_write(out, line, strlen(line));
                        storage_file_write(out, "\n", 1);
                    }
                }
            }
            li = 0;
            if(got != 1) break;
        }
    }
    storage_file_close(in);
    storage_file_free(in);
    storage_file_close(out);
    storage_file_free(out);

    if(updated) {
        storage_simply_remove(storage, path);
        storage_common_rename(storage, tmp, path);
    } else {
        storage_simply_remove(storage, tmp);
    }
    return updated;
}

uint32_t census_watchlist_busiest(Storage* storage, const char* place_id) {
    char path[160];
    census_place_file(place_id, "watchlist.csv", path, sizeof(path));
    File* f = storage_file_alloc(storage);
    uint32_t best_freq = 0;
    float best_occ = -1.0f;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[128];
        size_t li = 0;
        bool header = true;
        char c;
        while(storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 0) {
                    /* freq_hz,modulation,threshold_dbm,occupancy,source */
                    char* p = line;
                    uint32_t freq = (uint32_t)strtoul(p, &p, 10);
                    for(int col = 0; col < 3 && *p; col++) {
                        while(*p && *p != ',')
                            p++;
                        if(*p == ',') p++;
                    }
                    float occ = strtof(p, &p);
                    if(*p == ',') p++;
                    /* skip excluded entries (§6) */
                    if(strncmp(p, "user-exclude", 12) != 0 && occ > best_occ) {
                        best_occ = occ;
                        best_freq = freq;
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
    return best_freq;
}

/* --- SD health (§6.1) --- */
#define CENSUS_SD_LOW_BYTES (2u * 1024u * 1024u) /* stop writing captures below 2 MiB free */

bool census_sd_present(Storage* storage) {
    return storage_sd_status(storage) == FSE_OK;
}

bool census_sd_space(Storage* storage, uint64_t* free_bytes, uint64_t* total_bytes) {
    uint64_t total = 0, freeb = 0;
    FS_Error e = storage_common_fs_info(storage, STORAGE_EXT_PATH_PREFIX, &total, &freeb);
    if(e != FSE_OK) return false;
    if(total_bytes) *total_bytes = total;
    if(free_bytes) *free_bytes = freeb;
    return true;
}

bool census_sd_low(Storage* storage) {
    uint64_t freeb = 0;
    if(!census_sd_space(storage, &freeb, NULL)) return true; /* no card == can't write */
    return freeb < CENSUS_SD_LOW_BYTES;
}

bool census_storage_init(Storage* storage, CensusSettings* s) {
    FS_Error e = storage_common_mkdir(storage, CENSUS_BASE_DIR);
    if(e != FSE_OK && e != FSE_EXIST) return false;
    storage_common_mkdir(storage, CENSUS_SIGNATURES_DIR);
    storage_common_mkdir(storage, CENSUS_PLACES_DIR);

    char ids[CENSUS_MAX_PLACES][CENSUS_PLACE_ID_LEN];
    size_t n = census_place_list(storage, ids, CENSUS_MAX_PLACES);
    if(n == 0) {
        char id[CENSUS_PLACE_ID_LEN];
        census_place_create(storage, "Home", id, sizeof(id));
        strncpy(s->place_id, id, CENSUS_PLACE_ID_LEN - 1);
        s->place_id[CENSUS_PLACE_ID_LEN - 1] = '\0';
        census_settings_save(storage, s);
        return true;
    }
    /* if the active place no longer exists, fall back to the first */
    char dir[128];
    census_place_dir(s->place_id, dir, sizeof(dir));
    if(!storage_dir_exists(storage, dir)) {
        strncpy(s->place_id, ids[0], CENSUS_PLACE_ID_LEN - 1);
        s->place_id[CENSUS_PLACE_ID_LEN - 1] = '\0';
        census_settings_save(storage, s);
    }
    return true;
}
