/* census_worker.h — RSSI monitor + async-RX capture engine (Zero §5.1).
 *
 * Shared capture engine for Camp (§3.2), Sweep (§3.1), and Recon Stage B. Models the built-in
 * Read RAW path (async RX -> RAW timing stream) on the subghz_devices_* abstraction. On a hit
 * (RSSI >= threshold) it accumulates the level/duration stream into a signed-us timing buffer,
 * then when the signal ends (sub-threshold quiet for signal_end_gap, or capture_max elapsed)
 * finalizes: computes the feature vector (shared/core, §5.5), writes a standard `.sub`, appends
 * a census_log row, and notifies the UI.
 *
 * Live radio (RSSI/capture) needs real airtime — TODO(hw). The processing path it feeds is
 * shared/core and is unit-tested off-device (test/core). Monitoring is PASSIVE (never TX).
 */
#ifndef CENSUS_WORKER_H
#define CENSUS_WORKER_H

#include <furi.h>
#include <furi_hal_subghz.h>
#include <storage/storage.h>

#include "census_storage.h"

typedef struct CensusWorker CensusWorker;

/* Called (from the worker thread) when a capture has been logged; the scene refreshes. */
typedef void (*CensusWorkerCallback)(void* context);

typedef enum {
    CensusWorkerModeCamp = 0,
    CensusWorkerModeSweep = 1,
} CensusWorkerMode;

CensusWorker* census_worker_alloc(Storage* storage);
void census_worker_free(CensusWorker* worker);

void census_worker_set_callback(CensusWorker* worker, CensusWorkerCallback cb, void* context);

/* Configure from settings before starting. `preset` is a FuriHalSubGhzPreset. */
void census_worker_configure(
    CensusWorker* worker,
    const CensusSettings* settings,
    const char* place_id);

/* Camp on one frequency (§3.2). */
void census_worker_start_camp(CensusWorker* worker, uint32_t freq_hz);

/* Sweep a frequency list, dwelling `dwell_ms` on each (§3.1). */
void census_worker_start_sweep(
    CensusWorker* worker,
    const uint32_t* freqs,
    size_t count,
    uint32_t dwell_ms);

void census_worker_stop(CensusWorker* worker);
bool census_worker_is_running(CensusWorker* worker);

/* Live-view accessors (read by the scene's draw callback). */
float census_worker_rssi(CensusWorker* worker);
uint32_t census_worker_current_freq(CensusWorker* worker);
uint32_t census_worker_hits(CensusWorker* worker);

/* Most-recent capture summary for the live "recent hits" list (§6). */
typedef struct {
    uint32_t freq_hz;
    float rssi_dbm;
    char match[24]; /* best match name or "unknown" */
} CensusHit;

/* Copy up to `max` recent hits (newest first) into out; returns the count. */
size_t census_worker_recent_hits(CensusWorker* worker, CensusHit* out, size_t max);

#endif /* CENSUS_WORKER_H */
