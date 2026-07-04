#include "census_recon.h"

#include <furi_hal_rtc.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/core/sc_freq_bands.h"
#include "../shared/core/sc_occupancy.h"
#include "census_schema.h"

/* Known common channels (Zero §3.3 Stage A hybrid grid — fine resolution). */
static const uint32_t RECON_KNOWN[] = {
    303875000,
    310000000,
    315000000,
    318000000,
    345000000,
    390000000,
    418000000,
    433420000,
    433920000,
    434420000,
    868350000,
    915000000,
};
/* CC1101 legal segments [lo, hi] for the coarse background grid. */
static const uint32_t RECON_SEG[][2] = {
    {300000000, 348000000},
    {387000000, 464000000},
    {779000000, 928000000}};

#define RECON_DETECT_MARGIN 12.0f

struct CensusRecon {
    FuriThread* thread;
    Storage* storage;
    const SubGhzDevice* device;
    volatile bool running;

    char place_id[CENSUS_PLACE_ID_LEN];
    uint16_t survey_minutes;
    uint32_t step_hz;
    float threshold_dbm;
    bool fresh;

    uint32_t grid[CENSUS_RECON_MAX_BINS];
    size_t grid_n;
    ScOccupancyAccum accum[CENSUS_RECON_MAX_BINS];

    volatile uint32_t current_freq;
    volatile uint32_t hot_bins;
    volatile uint32_t pass;
    uint32_t start_tick;

    CensusReconProgress progress;
    void* progress_ctx;
};

size_t census_recon_build_grid(uint32_t step_hz, uint32_t* grid, size_t cap) {
    size_t n = 0;
    for(size_t i = 0; i < sizeof(RECON_KNOWN) / sizeof(RECON_KNOWN[0]) && n < cap; i++) {
        grid[n++] = RECON_KNOWN[i];
    }
    if(step_hz < 250000) step_hz = 250000;
    for(size_t s = 0; s < 3 && n < cap; s++) {
        for(uint32_t f = RECON_SEG[s][0]; f <= RECON_SEG[s][1] && n < cap; f += step_hz) {
            /* skip bins within half a step of a known channel (dedup) */
            bool near = false;
            for(size_t k = 0; k < n; k++) {
                uint32_t d = grid[k] > f ? grid[k] - f : f - grid[k];
                if(d < step_hz / 2) {
                    near = true;
                    break;
                }
            }
            if(!near) grid[n++] = f;
        }
    }
    return n;
}

static void recon_iso_now(char* out, size_t cap) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    snprintf(
        out,
        cap,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
}

/* Merge freshly-finished bins with the existing occupancy.csv (accumulate, System §9). */
static void recon_accumulate(CensusRecon* r, ScOccupancyBin* bins, size_t n) {
    char path[160];
    census_place_file(r->place_id, "occupancy.csv", path, sizeof(path));
    File* f = storage_file_alloc(r->storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[160];
        size_t li = 0;
        bool header = true;
        char c;
        while(storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 0) {
                    /* freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen */
                    ScOccupancyBin old = {0};
                    char* p = line;
                    old.freq_hz = (int32_t)strtol(p, &p, 10);
                    if(*p == ',') p++;
                    old.noise_floor = strtof(p, &p);
                    if(*p == ',') p++;
                    old.peak_rssi = strtof(p, &p);
                    if(*p == ',') p++;
                    old.occupancy = strtof(p, &p);
                    if(*p == ',') p++;
                    old.crossings = (int32_t)strtol(p, &p, 10);
                    for(size_t i = 0; i < n; i++) {
                        if(bins[i].freq_hz == old.freq_hz) {
                            sc_occupancy_merge(&bins[i], 1, &old, 1);
                            break;
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
}

static void recon_write_csvs(CensusRecon* r, ScOccupancyBin* bins, size_t n) {
    char ts[32];
    recon_iso_now(ts, sizeof(ts));

    /* occupancy.csv */
    char occ_path[160];
    census_place_file(r->place_id, "occupancy.csv", occ_path, sizeof(occ_path));
    File* f = storage_file_alloc(r->storage);
    if(storage_file_open(f, occ_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, OCCUPANCY_HEADER "\n", strlen(OCCUPANCY_HEADER) + 1);
        for(size_t i = 0; i < n; i++) {
            char row[128];
            int len = snprintf(
                row,
                sizeof(row),
                "%ld,%.1f,%.1f,%.4f,%ld,%s\n",
                (long)bins[i].freq_hz,
                (double)bins[i].noise_floor,
                (double)bins[i].peak_rssi,
                (double)bins[i].occupancy,
                (long)bins[i].crossings,
                ts);
            storage_file_write(f, row, len);
        }
    }
    storage_file_close(f);
    storage_file_free(f);

    /* watchlist.csv derived from occupancy (adaptive per-band thresholds = floor + margin) */
    ScWatchlistEntry wl[64];
    size_t nwl = sc_watchlist_from_occupancy(bins, n, 0.10f, RECON_DETECT_MARGIN, wl, 64);
    char wl_path[160];
    census_place_file(r->place_id, "watchlist.csv", wl_path, sizeof(wl_path));
    f = storage_file_alloc(r->storage);
    if(storage_file_open(f, wl_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, WATCHLIST_HEADER "\n", strlen(WATCHLIST_HEADER) + 1);
        for(size_t i = 0; i < nwl; i++) {
            char row[128];
            int len = snprintf(
                row,
                sizeof(row),
                "%ld,OOK,%.1f,%.4f,recon\n",
                (long)wl[i].freq_hz,
                (double)wl[i].threshold_dbm,
                (double)wl[i].occupancy);
            storage_file_write(f, row, len);
        }
    }
    storage_file_close(f);
    storage_file_free(f);
}

static int32_t census_recon_thread(void* context) {
    CensusRecon* r = context;
    r->grid_n = census_recon_build_grid(r->step_hz, r->grid, CENSUS_RECON_MAX_BINS);
    for(size_t i = 0; i < r->grid_n; i++)
        sc_occupancy_accum_init(&r->accum[i], (int32_t)r->grid[i]);

    subghz_devices_begin(r->device);
    subghz_devices_reset(r->device);
    subghz_devices_load_preset(r->device, FuriHalSubGhzPresetOok650Async, NULL);

    r->start_tick = furi_get_tick();
    uint32_t survey_ms = (uint32_t)r->survey_minutes * 60000u;

    while(r->running && (furi_get_tick() - r->start_tick) < survey_ms) {
        r->pass++;
        for(size_t i = 0; i < r->grid_n && r->running; i++) {
            uint32_t freq = r->grid[i];
            r->current_freq = freq;
            subghz_devices_idle(r->device);
            subghz_devices_set_frequency(r->device, freq);
            /* short dwell sampling RSSI (TODO(hw): needs real airtime) */
            uint32_t ts = furi_hal_rtc_get_timestamp();
            for(int s = 0; s < 4; s++) {
                float rssi = subghz_devices_get_rssi(r->device);
                sc_occupancy_accum_sample(&r->accum[i], rssi, r->threshold_dbm, ts);
                furi_delay_ms(2);
            }
        }
        /* hot-bin count for the live view */
        uint32_t hot = 0;
        for(size_t i = 0; i < r->grid_n; i++) {
            if(r->accum[i].above > 0) hot++;
        }
        r->hot_bins = hot;
        if(r->progress) r->progress(r->progress_ctx);
    }

    subghz_devices_idle(r->device);
    subghz_devices_end(r->device);

    /* finalize + emit artifacts */
    ScOccupancyBin* bins = malloc(sizeof(ScOccupancyBin) * r->grid_n);
    if(bins) {
        for(size_t i = 0; i < r->grid_n; i++)
            sc_occupancy_accum_finish(&r->accum[i], &bins[i]);
        if(!r->fresh) recon_accumulate(r, bins, r->grid_n);
        recon_write_csvs(r, bins, r->grid_n);
        free(bins);
    }
    FURI_LOG_I(
        "SubCensus",
        "SC scene=recon action=done bins=%u passes=%lu",
        (unsigned)r->grid_n,
        (unsigned long)r->pass);
    r->running = false;
    if(r->progress) r->progress(r->progress_ctx);
    return 0;
}

CensusRecon* census_recon_alloc(Storage* storage) {
    CensusRecon* r = malloc(sizeof(CensusRecon));
    memset(r, 0, sizeof(CensusRecon));
    r->storage = storage;
    /* subghz_devices_init() is owned by the app-lifetime census_worker; just fetch the device. */
    r->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    r->step_hz = 1000000;
    r->threshold_dbm = -80;
    r->survey_minutes = 15;
    return r;
}

void census_recon_free(CensusRecon* r) {
    census_recon_stop(r);
    free(r);
}

void census_recon_set_progress(CensusRecon* r, CensusReconProgress cb, void* context) {
    r->progress = cb;
    r->progress_ctx = context;
}

void census_recon_start(CensusRecon* r, const CensusSettings* s, const char* place_id, bool fresh) {
    if(r->running) return;
    strncpy(r->place_id, place_id, CENSUS_PLACE_ID_LEN - 1);
    r->place_id[CENSUS_PLACE_ID_LEN - 1] = '\0';
    r->survey_minutes = s->survey_minutes ? s->survey_minutes : 15;
    r->threshold_dbm = s->rssi_auto ? -80 : (float)s->rssi_threshold;
    r->fresh = fresh;
    r->pass = 0;
    r->hot_bins = 0;
    r->running = true;
    r->thread = furi_thread_alloc_ex("CensusRecon", 8192, census_recon_thread, r);
    furi_thread_start(r->thread);
}

void census_recon_stop(CensusRecon* r) {
    if(!r->running && !r->thread) return;
    r->running = false;
    if(r->thread) {
        furi_thread_join(r->thread);
        furi_thread_free(r->thread);
        r->thread = NULL;
    }
}

bool census_recon_is_running(CensusRecon* r) {
    return r->running;
}

uint32_t census_recon_current_freq(CensusRecon* r) {
    return r->current_freq;
}
uint32_t census_recon_hot_bins(CensusRecon* r) {
    return r->hot_bins;
}
uint32_t census_recon_pass(CensusRecon* r) {
    return r->pass;
}
uint32_t census_recon_elapsed_s(CensusRecon* r) {
    return (furi_get_tick() - r->start_tick) / 1000;
}
