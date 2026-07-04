// ============================================================================
// GENERATED FILE — DO NOT EDIT.
// Source of truth: shared/schema/*.schema.yaml
// Regenerate: python -m subcensus_tools.codegen   (from tools/)
// A schema/taxonomy change lands in shared/ and this file regenerates in the
// same commit (System §10) — so the tools cannot drift.
// ============================================================================

#pragma once

// Exact CSV headers, column counts, and column indices for each shared
// artifact. The FAP writes/reads against these so on-disk CSVs match the
// contract without hand-maintained magic strings.

// --- catalog_record (System §9, scope=catalog) ---
#define CATALOG_RECORD_HEADER "ts,freq_hz,modulation,device_class,first_seen,last_seen,count,match_name,match_class,match_conf,match_source,label,cadence_class,period_s,period_regularity,cadence_samples"
#define CATALOG_RECORD_NCOLS 16
typedef enum {
    CATALOG_RECORD_COL_TS = 0,
    CATALOG_RECORD_COL_FREQ_HZ = 1,
    CATALOG_RECORD_COL_MODULATION = 2,
    CATALOG_RECORD_COL_DEVICE_CLASS = 3,
    CATALOG_RECORD_COL_FIRST_SEEN = 4,
    CATALOG_RECORD_COL_LAST_SEEN = 5,
    CATALOG_RECORD_COL_COUNT = 6,
    CATALOG_RECORD_COL_MATCH_NAME = 7,
    CATALOG_RECORD_COL_MATCH_CLASS = 8,
    CATALOG_RECORD_COL_MATCH_CONF = 9,
    CATALOG_RECORD_COL_MATCH_SOURCE = 10,
    CATALOG_RECORD_COL_LABEL = 11,
    CATALOG_RECORD_COL_CADENCE_CLASS = 12,
    CATALOG_RECORD_COL_PERIOD_S = 13,
    CATALOG_RECORD_COL_PERIOD_REGULARITY = 14,
    CATALOG_RECORD_COL_CADENCE_SAMPLES = 15,
} CatalogRecordCol;

// --- census_log (Zero §5.4 (extends System §9), scope=per-place) ---
#define CENSUS_LOG_HEADER "ts_iso,freq_hz,rssi_dbm,duration_ms,preset,fsk_suspected,protocol,key,match_name,match_class,match_conf,match_source,sub_file,label"
#define CENSUS_LOG_NCOLS 14
typedef enum {
    CENSUS_LOG_COL_TS_ISO = 0,
    CENSUS_LOG_COL_FREQ_HZ = 1,
    CENSUS_LOG_COL_RSSI_DBM = 2,
    CENSUS_LOG_COL_DURATION_MS = 3,
    CENSUS_LOG_COL_PRESET = 4,
    CENSUS_LOG_COL_FSK_SUSPECTED = 5,
    CENSUS_LOG_COL_PROTOCOL = 6,
    CENSUS_LOG_COL_KEY = 7,
    CENSUS_LOG_COL_MATCH_NAME = 8,
    CENSUS_LOG_COL_MATCH_CLASS = 9,
    CENSUS_LOG_COL_MATCH_CONF = 10,
    CENSUS_LOG_COL_MATCH_SOURCE = 11,
    CENSUS_LOG_COL_SUB_FILE = 12,
    CENSUS_LOG_COL_LABEL = 13,
} CensusLogCol;

// --- fingerprints (System §7, scope=global) ---
#define FINGERPRINTS_HEADER "id,freq_bin,modulation,sym_dur_us_1,sym_dur_us_2,sym_dur_us_3,n_symbols,est_bitrate,preamble_len,repeat_count,device_name,device_class,source,cadence_class,period_s,period_regularity,cadence_samples"
#define FINGERPRINTS_NCOLS 17
typedef enum {
    FINGERPRINTS_COL_ID = 0,
    FINGERPRINTS_COL_FREQ_BIN = 1,
    FINGERPRINTS_COL_MODULATION = 2,
    FINGERPRINTS_COL_SYM_DUR_US_1 = 3,
    FINGERPRINTS_COL_SYM_DUR_US_2 = 4,
    FINGERPRINTS_COL_SYM_DUR_US_3 = 5,
    FINGERPRINTS_COL_N_SYMBOLS = 6,
    FINGERPRINTS_COL_EST_BITRATE = 7,
    FINGERPRINTS_COL_PREAMBLE_LEN = 8,
    FINGERPRINTS_COL_REPEAT_COUNT = 9,
    FINGERPRINTS_COL_DEVICE_NAME = 10,
    FINGERPRINTS_COL_DEVICE_CLASS = 11,
    FINGERPRINTS_COL_SOURCE = 12,
    FINGERPRINTS_COL_CADENCE_CLASS = 13,
    FINGERPRINTS_COL_PERIOD_S = 14,
    FINGERPRINTS_COL_PERIOD_REGULARITY = 15,
    FINGERPRINTS_COL_CADENCE_SAMPLES = 16,
} FingerprintsCol;

// --- occupancy (System §9, scope=per-place) ---
#define OCCUPANCY_HEADER "freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen"
#define OCCUPANCY_NCOLS 6
typedef enum {
    OCCUPANCY_COL_FREQ_HZ = 0,
    OCCUPANCY_COL_NOISE_FLOOR = 1,
    OCCUPANCY_COL_PEAK_RSSI = 2,
    OCCUPANCY_COL_OCCUPANCY = 3,
    OCCUPANCY_COL_CROSSINGS = 4,
    OCCUPANCY_COL_LAST_SEEN = 5,
} OccupancyCol;

// --- protocol_map (System §6, scope=global) ---
#define PROTOCOL_MAP_HEADER "protocol,friendly_name,device_class,typical_use,notes"
#define PROTOCOL_MAP_NCOLS 5
typedef enum {
    PROTOCOL_MAP_COL_PROTOCOL = 0,
    PROTOCOL_MAP_COL_FRIENDLY_NAME = 1,
    PROTOCOL_MAP_COL_DEVICE_CLASS = 2,
    PROTOCOL_MAP_COL_TYPICAL_USE = 3,
    PROTOCOL_MAP_COL_NOTES = 4,
} ProtocolMapCol;

// --- watchlist (System §9, scope=per-place) ---
#define WATCHLIST_HEADER "freq_hz,modulation,threshold_dbm,occupancy,source"
#define WATCHLIST_NCOLS 5
typedef enum {
    WATCHLIST_COL_FREQ_HZ = 0,
    WATCHLIST_COL_MODULATION = 1,
    WATCHLIST_COL_THRESHOLD_DBM = 2,
    WATCHLIST_COL_OCCUPANCY = 3,
    WATCHLIST_COL_SOURCE = 4,
} WatchlistCol;
