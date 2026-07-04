/* census_storage.h — settings persistence + Places on-disk model (Zero §4, §5.4, §5.6).
 *
 * Per-place layout under /ext/apps_data/subcensuszero (System §4). Global signatures/ sits
 * at the root and is NEVER touched by place delete. Settings persist to config.settings.
 */
#ifndef CENSUS_STORAGE_H
#define CENSUS_STORAGE_H

#include <furi.h>
#include <storage/storage.h>

#define CENSUS_BASE_DIR       EXT_PATH("apps_data/subcensuszero")
#define CENSUS_SIGNATURES_DIR CENSUS_BASE_DIR "/signatures"
#define CENSUS_PLACES_DIR     CENSUS_BASE_DIR "/places"
#define CENSUS_CONFIG_PATH    CENSUS_BASE_DIR "/config.settings"

#define CENSUS_PLACE_ID_LEN   40
#define CENSUS_PLACE_NAME_LEN 32
#define CENSUS_MAX_PLACES     32

typedef enum {
    CensusModeRecon = 0,
    CensusModeSweep = 1,
    CensusModeCamp = 2,
} CensusMode;

typedef enum {
    CensusCaptureOok650 = 0,
    CensusCaptureOok270 = 1,
    CensusCaptureFsk = 2,
    CensusCaptureDual = 3,
    CensusCapturePresetCount = 4,
} CensusCapturePreset;

typedef enum {
    CensusNotifyOff = 0,
    CensusNotifyLed = 1,
    CensusNotifyLedVibro = 2,
} CensusNotify;

/* Persisted app settings + active place (Zero §4). */
typedef struct {
    char place_id[CENSUS_PLACE_ID_LEN];
    uint8_t mode; /* CensusMode */
    uint8_t freq_preset; /* CensusFreqPreset */
    bool use_watchlist;
    bool rssi_auto; /* Auto threshold (sample noise floor + margin) */
    int32_t rssi_threshold; /* dBm, used when !rssi_auto */
    uint8_t capture_preset; /* CensusCapturePreset */
    uint32_t dwell_ms;
    uint32_t capture_max_ms;
    uint32_t signal_end_gap_ms;
    uint32_t min_gap_ms;
    uint16_t survey_minutes;
    bool auto_classify;
    bool match_db;
    uint8_t notify; /* CensusNotify */
} CensusSettings;

void census_settings_set_defaults(CensusSettings* s);
bool census_settings_load(Storage* storage, CensusSettings* s);
bool census_settings_save(Storage* storage, const CensusSettings* s);

/* Ensure base dirs + a default "home" place exist on first run. Returns false on SD error. */
bool census_storage_init(Storage* storage, CensusSettings* s);

/* Slugify a display name into a filesystem-safe place_id (slug + short hash). */
void census_place_id_from_name(const char* name, char* out_id, size_t cap);

/* Create a place folder (place.meta + empty occupancy/watchlist/census_log with headers).
 * Returns false on error or if it already exists. */
bool census_place_create(Storage* storage, const char* name, char* out_id, size_t cap);

/* List place ids into ids[CENSUS_MAX_PLACES][CENSUS_PLACE_ID_LEN]. Returns the count. */
size_t census_place_list(Storage* storage, char ids[][CENSUS_PLACE_ID_LEN], size_t max);

/* Read a place's display name from its place.meta into out. */
bool census_place_name(Storage* storage, const char* place_id, char* out, size_t cap);

/* Rename a place's display name in place.meta (id stays fixed — rename-safe, Zero §4). */
bool census_place_rename(Storage* storage, const char* place_id, const char* new_name);

/* Delete a place folder (recursive). NEVER touches signatures/. Refuses the active place
 * unless it is not the last one. Returns false on error. */
bool census_place_delete(Storage* storage, const char* place_id);

/* Absolute path helpers into the active/other place. */
void census_place_dir(const char* place_id, char* out, size_t cap);
void census_place_file(const char* place_id, const char* filename, char* out, size_t cap);

#endif /* CENSUS_STORAGE_H */
