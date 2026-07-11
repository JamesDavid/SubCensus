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
    319500000, /* GE/Interlogix/2GIG security sensors (door/window/motion) — US alarm band */
    345000000, /* Honeywell 5800-series security sensors */
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
    uint8_t grid_mode;
    float threshold_dbm;
    bool fresh;

    uint32_t grid[CENSUS_RECON_MAX_BINS];
    size_t grid_n;
    ScOccupancyAccum accum[CENSUS_RECON_MAX_BINS];
    volatile size_t sniff_edges; /* edge count during a Stage B modulation sniff (§3.3 Stage B) */

    volatile uint32_t current_freq;
    volatile uint32_t hot_bins;
    volatile uint32_t pass;
    uint32_t start_tick;

    CensusReconProgress progress;
    void* progress_ctx;
};

size_t census_recon_build_grid(uint8_t grid_mode, uint32_t step_hz, uint32_t* grid, size_t cap) {
    size_t n = 0;
    /* Full-band is a uniform coarse grid (no special known channels); the other two include
     * the fine known list; Known-only stops there (§3.3 Stage A / §4). */
    bool include_known = (grid_mode != CensusReconGridFull);
    bool include_coarse = (grid_mode != CensusReconGridKnown);
    if(include_known) {
        for(size_t i = 0; i < sizeof(RECON_KNOWN) / sizeof(RECON_KNOWN[0]) && n < cap; i++) {
            grid[n++] = RECON_KNOWN[i];
        }
    }
    if(step_hz < 250000) step_hz = 250000;
    for(size_t s = 0; s < 3 && include_coarse && n < cap; s++) {
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

/* Collect the freqs of existing user-pin / user-exclude watchlist rows so a regenerated
 * watchlist preserves them (System §9). Returns counts via *np / *nx. */
static void recon_read_user_rows(
    CensusRecon* r,
    uint32_t* pinned,
    size_t* np,
    uint32_t* excluded,
    size_t* nx,
    size_t cap) {
    *np = 0;
    *nx = 0;
    char path[160];
    census_place_file(r->place_id, "watchlist.csv", path, sizeof(path));
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
                    uint32_t freq = (uint32_t)strtoul(line, NULL, 10);
                    /* find col 4 (source) */
                    const char* p = line;
                    for(int col = 0; col < 4 && *p; col++) {
                        while(*p && *p != ',')
                            p++;
                        if(*p == ',') p++;
                    }
                    if(strncmp(p, "user-pin", 8) == 0 && *np < cap)
                        pinned[(*np)++] = freq;
                    else if(strncmp(p, "user-exclude", 12) == 0 && *nx < cap)
                        excluded[(*nx)++] = freq;
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

static bool freq_in(const uint32_t* list, size_t n, uint32_t f) {
    for(size_t i = 0; i < n; i++)
        if(list[i] == f) return true;
    return false;
}

/* Stage B edge counter (interrupt context — keep minimal). */
static void recon_sniff_cb(bool level, uint32_t duration, void* context) {
    CensusRecon* r = context;
    (void)level;
    (void)duration;
    r->sniff_edges++;
}

/* Stage B modulation sniff (§3.3): capture short windows under OOK and 2-FSK on `freq` and keep
 * whichever yields more pulse structure — that resolves modulation, which RSSI alone can't. The
 * dual-preset compare is real; the live capture needs a real signal (TODO(hw)), so with no
 * airtime both windows read empty and it defaults to OOK. Requires the device to be begun. */
static const char* recon_sniff_modulation(CensusRecon* r, uint32_t freq) {
    static const FuriHalSubGhzPreset presets[2] = {
        FuriHalSubGhzPresetOok650Async, FuriHalSubGhzPreset2FSKDev476Async};
    static const char* const names[2] = {"OOK", "2FSK"};
    size_t best = 0;
    int bi = 0;
    for(int p = 0; p < 2; p++) {
        subghz_devices_idle(r->device);
        subghz_devices_set_frequency(r->device, freq);
        subghz_devices_load_preset(r->device, presets[p], NULL);
        r->sniff_edges = 0;
        subghz_devices_start_async_rx(r->device, recon_sniff_cb, r);
        furi_delay_ms(150); /* short refine window (TODO(hw): needs a live signal) */
        subghz_devices_stop_async_rx(r->device);
        subghz_devices_idle(r->device);
        if(r->sniff_edges > best) {
            best = r->sniff_edges;
            bi = p;
        }
    }
    return names[bi];
}

static void recon_write_csvs(CensusRecon* r, ScOccupancyBin* bins, size_t n) {
    char ts[32];
    recon_iso_now(ts, sizeof(ts));

    /* preserve user pins/exclusions across the regeneration (System §9) */
    uint32_t pinned[32], excluded[32];
    size_t np = 0, nx = 0;
    recon_read_user_rows(r, pinned, &np, excluded, &nx, 32);

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
            uint32_t freq = (uint32_t)wl[i].freq_hz;
            if(freq_in(excluded, nx, freq)) continue; /* excluded: omit (§9) */
            const char* src = freq_in(pinned, np, freq) ? "user-pin" : "recon";
            const char* mod = recon_sniff_modulation(r, freq); /* Stage B resolve (§3.3) */
            char row[128];
            int len = snprintf(
                row,
                sizeof(row),
                "%ld,%s,%.1f,%.4f,%s\n",
                (long)wl[i].freq_hz,
                mod,
                (double)wl[i].threshold_dbm,
                (double)wl[i].occupancy,
                src);
            storage_file_write(f, row, len);
        }
        /* re-add pinned freqs the recon pass didn't surface (§9 preserve pins) */
        for(size_t i = 0; i < np; i++) {
            bool present = false;
            for(size_t j = 0; j < nwl; j++)
                if((uint32_t)wl[j].freq_hz == pinned[i]) present = true;
            if(present || freq_in(excluded, nx, pinned[i])) continue;
            char row[128];
            int len = snprintf(
                row, sizeof(row), "%lu,OOK,-80.0,0.0000,user-pin\n", (unsigned long)pinned[i]);
            storage_file_write(f, row, len);
        }
    }
    storage_file_close(f);
    storage_file_free(f);
}

static int32_t census_recon_thread(void* context) {
    CensusRecon* r = context;
    r->grid_n = census_recon_build_grid(r->grid_mode, r->step_hz, r->grid, CENSUS_RECON_MAX_BINS);
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
            /* Enter RX before reading RSSI: the CC1101's RSSI register is only valid in the RX
             * state. Without this the reads come back a flat floor and no bin ever crosses the
             * threshold (occupancy stayed ~0 / "hot 0"), so recon was effectively RF-blind. The
             * chip also needs a brief settle after strobing RX before the first sample is good. */
            subghz_devices_set_rx(r->device);
            furi_delay_ms(3);
            /* short dwell sampling RSSI (TODO(hw): dwell is brief — occupancy accrues over passes,
             * it is NOT a camp/decode, so a single transient burst can fall between samples). */
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

    /* finalize + emit artifacts (Stage B modulation sniff in recon_write_csvs needs the device
     * still begun, so do this BEFORE ending it). */
    ScOccupancyBin* bins = malloc(sizeof(ScOccupancyBin) * r->grid_n);
    if(bins) {
        for(size_t i = 0; i < r->grid_n; i++)
            sc_occupancy_accum_finish(&r->accum[i], &bins[i]);
        if(!r->fresh) recon_accumulate(r, bins, r->grid_n);
        recon_write_csvs(r, bins, r->grid_n);
        free(bins);
    }

    subghz_devices_idle(r->device);
    subghz_devices_end(r->device);
    FURI_LOG_I(
        "SubCensus",
        "SC scene=recon action=done bins=%u passes=%lu",
        (unsigned)r->grid_n,
        (unsigned long)r->pass);
    r->running = false;
    if(r->progress) r->progress(r->progress_ctx);
    return 0;
}

void census_recon_reset(Storage* storage, const char* place_id, bool keep_pins) {
    /* occupancy.csv -> header only */
    char occ_path[160];
    census_place_file(place_id, "occupancy.csv", occ_path, sizeof(occ_path));
    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, occ_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, OCCUPANCY_HEADER "\n", strlen(OCCUPANCY_HEADER) + 1);
    }
    storage_file_close(f);
    storage_file_free(f);

    /* watchlist.csv -> keep only user rows (keep_pins) or empty */
    char wl_path[160];
    census_place_file(place_id, "watchlist.csv", wl_path, sizeof(wl_path));
    char* keep = malloc(2048);
    if(!keep) return;
    size_t out = 0;
    out += (size_t)snprintf(keep + out, 2048 - out, "%s\n", WATCHLIST_HEADER);
    if(keep_pins) {
        File* rf = storage_file_alloc(storage);
        if(storage_file_open(rf, wl_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            char line[160];
            size_t li = 0;
            bool header = true;
            char c;
            while(storage_file_read(rf, &c, 1) == 1) {
                if(c == '\n' || li >= sizeof(line) - 1) {
                    line[li] = '\0';
                    if(!header && li > 0 && out < 1900) {
                        const char* p = line;
                        for(int col = 0; col < 4 && *p; col++) {
                            while(*p && *p != ',')
                                p++;
                            if(*p == ',') p++;
                        }
                        if(strncmp(p, "user-", 5) == 0)
                            out += (size_t)snprintf(keep + out, 2048 - out, "%s\n", line);
                    }
                    header = false;
                    li = 0;
                } else if(c != '\r') {
                    line[li++] = c;
                }
            }
        }
        storage_file_close(rf);
        storage_file_free(rf);
    }
    File* w = storage_file_alloc(storage);
    if(storage_file_open(w, wl_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(w, keep, out);
    }
    storage_file_close(w);
    storage_file_free(w);
    free(keep);
}

CensusRecon* census_recon_alloc(Storage* storage) {
    CensusRecon* r = malloc(sizeof(CensusRecon));
    memset(r, 0, sizeof(CensusRecon));
    r->storage = storage;
    /* subghz_devices_init() is app-lifetime (subcensuszero.c app_alloc); just fetch the device. */
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
    r->step_hz = s->recon_step_hz ? s->recon_step_hz : 250000;
    r->grid_mode = s->recon_grid;
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

/* --- spectrum-strip live view (§6) --- */

uint8_t census_recon_current_segment(CensusRecon* r) {
    uint32_t f = r->current_freq;
    for(uint8_t s = 0; s < 3; s++) {
        if(f >= RECON_SEG[s][0] && f <= RECON_SEG[s][1]) return s;
    }
    return 0;
}

void census_recon_segment_bounds(uint8_t seg, uint32_t* lo, uint32_t* hi) {
    if(seg > 2) seg = 2;
    if(lo) *lo = RECON_SEG[seg][0];
    if(hi) *hi = RECON_SEG[seg][1];
}

size_t census_recon_segment_bars(CensusRecon* r, uint8_t seg, float* bars, size_t nbars) {
    if(seg > 2) seg = 2;
    uint32_t lo = RECON_SEG[seg][0], hi = RECON_SEG[seg][1];
    uint32_t span = hi - lo;
    for(size_t i = 0; i < nbars; i++)
        bars[i] = CENSUS_RSSI_NONE;
    if(span == 0) return nbars;
    for(size_t i = 0; i < r->grid_n; i++) {
        uint32_t f = (uint32_t)r->accum[i].freq_hz;
        if(f < lo || f > hi) continue;
        size_t b = (size_t)(((uint64_t)(f - lo) * nbars) / span);
        if(b >= nbars) b = nbars - 1;
        float pk = r->accum[i].started ? r->accum[i].peak_rssi : CENSUS_RSSI_NONE;
        if(pk > bars[b]) bars[b] = pk; /* peak-hold across bins in the bucket */
    }
    return nbars;
}

size_t census_recon_top_hits(CensusRecon* r, uint32_t* freqs, float* peaks, size_t max) {
    size_t n = 0;
    /* simple selection of the top `max` bins by peak RSSI among above-floor bins */
    for(size_t k = 0; k < max; k++) {
        int best = -1;
        for(size_t i = 0; i < r->grid_n; i++) {
            if(r->accum[i].above <= 0) continue;
            /* skip already-selected */
            bool used = false;
            for(size_t j = 0; j < n; j++)
                if(freqs[j] == (uint32_t)r->accum[i].freq_hz) used = true;
            if(used) continue;
            if(best < 0 || r->accum[i].peak_rssi > r->accum[best].peak_rssi) best = (int)i;
        }
        if(best < 0) break;
        freqs[n] = (uint32_t)r->accum[best].freq_hz;
        peaks[n] = r->accum[best].peak_rssi;
        n++;
    }
    return n;
}
