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
    s->auto_classify = true;
    s->match_db = true;
    s->notify = CensusNotifyLed;
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
    WU("auto_classify", s->auto_classify);
    WU("match_db", s->match_db);
    WU("notify", s->notify);
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
    RU("auto_classify", s->auto_classify);
    RU("match_db", s->match_db);
    RU("notify", s->notify);
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

static bool census_place_write_meta(Storage* storage, const char* place_id, const char* name) {
    char path[128];
    census_place_file(place_id, "place.meta", path, sizeof(path));
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = flipper_format_file_open_always(ff, path);
    if(ok) ok = flipper_format_write_header_cstr(ff, PLACE_META_HEADER, 1);
    if(ok) ok = flipper_format_write_string_cstr(ff, "name", name);
    uint32_t created = furi_hal_rtc_get_timestamp();
    if(ok) ok = flipper_format_write_uint32(ff, "created", &created, 1);
    if(ok) ok = flipper_format_write_string_cstr(ff, "notes", "");
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

    bool ok = census_place_write_meta(storage, id, name);
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

bool census_place_rename(Storage* storage, const char* place_id, const char* new_name) {
    /* preserve created timestamp; only the display name changes (id is rename-safe) */
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
    if(ok) ok = flipper_format_write_string_cstr(ff, "name", new_name);
    if(ok) ok = flipper_format_write_uint32(ff, "created", &created, 1);
    if(ok) ok = flipper_format_write_string_cstr(ff, "notes", "");
    flipper_format_free(ff);
    return ok;
}

bool census_place_delete(Storage* storage, const char* place_id) {
    char dir[128];
    census_place_dir(place_id, dir, sizeof(dir));
    return storage_simply_remove_recursive(storage, dir);
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
