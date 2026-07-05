/* census_recon.h — Recon occupancy survey (Zero §3.3): stepped-RSSI sweep over a hybrid grid
 * -> occupancy.csv, then derive watchlist.csv (shared schema, System §9). Cumulative per place
 * (accumulate/fresh). The occupancy accumulation + watchlist derivation reuse shared/core
 * (sc_occupancy). Live RSSI needs real airtime (TODO(hw)); the grid + stats + CSV emit are real.
 */
#ifndef CENSUS_RECON_H
#define CENSUS_RECON_H

#include <furi.h>
#include <storage/storage.h>

#include "census_storage.h"

#define CENSUS_RECON_MAX_BINS 256

typedef struct CensusRecon CensusRecon;

/* Progress callback (from the recon thread): current segment freq, hot-bin count, pass count. */
typedef void (*CensusReconProgress)(void* context);

CensusRecon* census_recon_alloc(Storage* storage);
void census_recon_free(CensusRecon* recon);
void census_recon_set_progress(CensusRecon* recon, CensusReconProgress cb, void* context);

/* Start a survey over `survey_minutes`. `fresh` clears occupancy.csv first; else accumulates
 * into it (System §9). Writes occupancy.csv + watchlist.csv on completion. */
void census_recon_start(
    CensusRecon* recon,
    const CensusSettings* settings,
    const char* place_id,
    bool fresh);
void census_recon_stop(CensusRecon* recon);
bool census_recon_is_running(CensusRecon* recon);

/* Live-view accessors. */
uint32_t census_recon_current_freq(CensusRecon* recon);
uint32_t census_recon_hot_bins(CensusRecon* recon);
uint32_t census_recon_pass(CensusRecon* recon);
uint32_t census_recon_elapsed_s(CensusRecon* recon);

/* --- spectrum-strip live view (§6) --- */
#define CENSUS_RECON_SEGMENTS 3

/* The CC1101 legal segment the sweep is currently in (0..2). */
uint8_t census_recon_current_segment(CensusRecon* recon);
/* Segment [lo,hi] bounds + short label for segment `seg`. */
void census_recon_segment_bounds(uint8_t seg, uint32_t* lo, uint32_t* hi);

/* Downsample segment `seg` into `nbars` RSSI buckets (peak-hold across the bins in each bucket),
 * writing dBm into bars[nbars] (SC_RSSI_NONE where no bin/sample). Returns nbars. */
#define CENSUS_RSSI_NONE (-127.0f)
size_t census_recon_segment_bars(CensusRecon* recon, uint8_t seg, float* bars, size_t nbars);

/* Top-N hottest bins so far (peak RSSI, above-floor). Fills freqs[]/peaks[]; returns the count. */
size_t census_recon_top_hits(CensusRecon* recon, uint32_t* freqs, float* peaks, size_t max);

/* Reset this place's recon artifacts (§6 / System §9, confirm-gated by the caller): clears
 * occupancy.csv to header-only and regenerates watchlist.csv. `keep_pins` preserves user-pin /
 * user-exclude rows; otherwise the watchlist is emptied too. Captures / census_log / global
 * signatures are untouched. */
void census_recon_reset(Storage* storage, const char* place_id, bool keep_pins);

/* Build the recon grid into grid[cap]; returns count. `grid_mode` is a CensusReconGrid:
 * Hybrid (known + coarse background), Known-only (fine known channels), or Full-band (uniform
 * coarse across the whole segments). `step_hz` is the coarse-grid granularity (§3.3 Stage A). */
size_t census_recon_build_grid(uint8_t grid_mode, uint32_t step_hz, uint32_t* grid, size_t cap);

#endif /* CENSUS_RECON_H */
