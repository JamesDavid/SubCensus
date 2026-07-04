/* sc_occupancy.h — Recon occupancy accumulation + watchlist derivation (System §9, §3.3).
 *
 * Recon is cumulative (a single pass misses periodic emitters that didn't fire while their
 * bin was sampled), so occupancy bins ACCUMULATE across passes: rolling/max peak RSSI,
 * summed crossings, recomputed occupancy, updated last-seen (System §9). The watchlist is
 * derived from the accumulated bins with per-band adaptive thresholds (Stage C).
 */
#ifndef SC_OCCUPANCY_H
#define SC_OCCUPANCY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_types.h"

typedef struct {
    int32_t freq_hz;
    double noise_floor; /* dBm */
    double peak_rssi;   /* dBm */
    double occupancy;   /* 0..1 fraction of samples above threshold */
    int32_t crossings;  /* below->above transitions */
    int64_t last_seen;  /* epoch seconds (CSV stores ISO; core uses epoch) */
} ScOccupancyBin;

/* Online accumulator for one bin during a live sweep pass. Threshold is supplied per
 * sample (Auto noise-floor calibration or watchlist per-band threshold, §4). */
typedef struct {
    int32_t freq_hz;
    double peak_rssi;
    double noise_floor; /* min RSSI seen (floor proxy) */
    int32_t above;
    int32_t total;
    int32_t crossings;
    int64_t last_seen;
    bool prev_above;
    bool started;
} ScOccupancyAccum;

void sc_occupancy_accum_init(ScOccupancyAccum* a, int32_t freq_hz);
void sc_occupancy_accum_sample(ScOccupancyAccum* a, double rssi_dbm, double threshold_dbm, int64_t ts_s);
void sc_occupancy_accum_finish(const ScOccupancyAccum* a, ScOccupancyBin* out);

/* Merge `in` into `acc` (same freq bin) across passes. Weights are sample counts (pass 1
 * vs 2); pass 1,1 for a simple CSV-to-CSV accumulate. peak=max, crossings summed,
 * occupancy + noise_floor weighted-averaged, last_seen=max (System §9). */
void sc_occupancy_merge(ScOccupancyBin* acc, int32_t acc_weight, const ScOccupancyBin* in, int32_t in_weight);

/* --- watchlist derivation (Stage C) --- */

typedef struct {
    int32_t freq_hz;
    ScModulation modulation; /* SC_MOD_UNKNOWN until Stage B resolves it */
    double threshold_dbm;    /* noise_floor + margin (adaptive per band) */
    double occupancy;
} ScWatchlistEntry;

/* Derive watchlist entries from occupancy bins with occupancy >= cutoff, threshold =
 * noise_floor + margin_db. Writes up to `cap` entries sorted by occupancy DESC (so entry 0
 * is the Auto=busiest pick, §3.2). Returns the count. */
size_t sc_watchlist_from_occupancy(
    const ScOccupancyBin* bins,
    size_t n,
    double occ_cutoff,
    double margin_db,
    ScWatchlistEntry* out,
    size_t cap);

#endif /* SC_OCCUPANCY_H */
