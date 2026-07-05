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
#define CENSUS_CUSTOM_MAX     16 /* custom frequency-list capacity (§4 Custom preset) */

typedef enum {
    CensusModeRecon = 0,
    CensusModeSweep = 1,
    CensusModeCamp = 2,
} CensusMode;

/* Recon Stage A grid selection (§3.3 / §4). */
typedef enum {
    CensusReconGridHybrid = 0, /* known channels + coarse background (default) */
    CensusReconGridKnown = 1, /* known common channels only */
    CensusReconGridFull = 2, /* full coarse background across all segments */
    CensusReconGridCount = 3,
} CensusReconGrid;

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
    uint8_t recon_grid; /* CensusReconGrid (§3.3 Stage A) */
    uint32_t recon_step_hz; /* coarse background grid step / RX BW (§4) */
    uint32_t camp_freq_hz; /* Camp default frequency; 0 = Auto (busiest watchlist) (§3.2) */
    bool auto_classify;
    bool match_db;
    uint8_t notify; /* CensusNotify */
    uint8_t custom_count; /* Custom preset list length (§4) */
    uint32_t custom_freqs[CENSUS_CUSTOM_MAX];
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

/* Read/write the place's optional location tag (manual text, §5.6). */
void census_place_location(Storage* storage, const char* place_id, char* out, size_t cap);
bool census_place_set_location(Storage* storage, const char* place_id, const char* location);

/* Delete a place folder (recursive). NEVER touches signatures/. Refuses the active place
 * unless it is not the last one. Returns false on error. */
bool census_place_delete(Storage* storage, const char* place_id);

/* Absolute path helpers into the active/other place. */
void census_place_dir(const char* place_id, char* out, size_t cap);
void census_place_file(const char* place_id, const char* filename, char* out, size_t cap);

/* Load watchlist frequencies (freq_hz column) for a place into out[cap]; returns the count.
 * 0 if the watchlist is absent/empty (Sweep then falls back to the preset list, System §9). */
size_t census_watchlist_freqs(Storage* storage, const char* place_id, uint32_t* out, size_t cap);

/* Load watchlist freqs + their adaptive per-band thresholds (col threshold_dbm) into
 * out_freqs[cap]/out_thr[cap], skipping user-exclude rows. Returns the count (§3.1/§3.3 Stage C).
 * out_thr may be NULL if only frequencies are wanted. */
size_t census_watchlist_load(
    Storage* storage,
    const char* place_id,
    uint32_t* out_freqs,
    float* out_thr,
    size_t cap);

/* The adaptive threshold for a single watchlist frequency (Camp §3.2). Returns true + *out_thr
 * if `freq` is a (non-excluded) watchlist entry; false otherwise (caller uses Auto/global). */
bool census_watchlist_threshold(
    Storage* storage,
    const char* place_id,
    uint32_t freq,
    float* out_thr);

/* The busiest watchlist entry (highest occupancy) for Camp Auto (§3.2). 0 if no watchlist. */
uint32_t census_watchlist_busiest(Storage* storage, const char* place_id);

/* Set the `source` column of the watchlist entry at `freq` to `source` ("user-pin" /
 * "user-exclude" / "recon"), inserting a row if `freq` isn't present (Recon results Pin/Exclude,
 * §6 / System §9). Returns true on success. */
bool census_watchlist_set_source(
    Storage* storage,
    const char* place_id,
    uint32_t freq,
    const char* source);

/* Rewrite the `label` column (in place) of the census_log row whose sub_file matches (Zero §6
 * Review label picker). Returns true if a matching row was updated. */
bool census_log_set_label(
    Storage* storage,
    const char* place_id,
    const char* sub_file,
    const char* label);

/* --- SD health (§6.1) --- */
/* True if /ext (the SD card) is mounted and the app base dir is usable. */
bool census_sd_present(Storage* storage);
/* Free/total bytes on /ext. Returns false if the card is absent. */
bool census_sd_space(Storage* storage, uint64_t* free_bytes, uint64_t* total_bytes);
/* True when free space is below the low-space cutoff (stop writing captures, §6.1 SD full). */
bool census_sd_low(Storage* storage);

#endif /* CENSUS_STORAGE_H */
