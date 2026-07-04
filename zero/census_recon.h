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

/* Build the hybrid grid (known channels + coarse background) into grid[cap]; returns count.
 * Exposed for testing / reuse; step_hz is the coarse-grid granularity (§3.3 Stage A). */
size_t census_recon_build_grid(uint32_t step_hz, uint32_t* grid, size_t cap);

#endif /* CENSUS_RECON_H */
