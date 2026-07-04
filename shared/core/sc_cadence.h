/* sc_cadence.h — signal cadence / temporal fingerprint (System §7a).
 *
 * Cadence is a PER-DEVICE property derived from a device's reception history, not from a
 * single capture. Two entry points:
 *   - batch: sc_cadence_from_timestamps() — full-precision estimate from pooled event
 *     history (host merge in build_signatures.py recomputes canonically, System §7a).
 *   - running: a compact estimator (last_ts, count, running mean/variance, small interval
 *     histogram for harmonic folding) for flash-limited live sensors that must NOT store a
 *     full event log (System §7a). The Zero is a weak cadence measurer (Zero §5.5); the
 *     stationary Pi/Esp are strong.
 *
 * Dropout-robust: missed receptions turn true intervals into 2x/3x harmonics, so a naive
 * median is biased. We estimate the FUNDAMENTAL (smallest strongly-supported cluster) and
 * fold longer intervals onto it (System §7a).
 */
#ifndef SC_CADENCE_H
#define SC_CADENCE_H

#include <stddef.h>
#include <stdint.h>

#include "sc_types.h"

#define SC_CADENCE_HIST_BINS 48

typedef struct {
    ScCadenceClass cls;
    double period_s;    /* estimated fundamental; 0 for event-driven/seen-once (= null) */
    double regularity;  /* 0..1, 1 = metronomic; 1 - min(1, CoV) (System §7a) */
    int32_t samples;    /* number of inter-arrival intervals behind the estimate */
} ScCadenceEstimate;

/* Estimate cadence from `n` reception timestamps (seconds; need not be pre-sorted). */
void sc_cadence_from_timestamps(const int64_t* ts_s, size_t n, ScCadenceEstimate* out);

/* --- compact running estimator (no event log) --- */

typedef struct {
    int64_t last_ts;
    int32_t count;
    double interval_sum;
    double interval_sqsum;
    int32_t n_intervals;
    double bin_width_s;               /* histogram bin width (seconds) */
    int32_t hist[SC_CADENCE_HIST_BINS];
} ScCadenceEstimator;

void sc_cadence_init(ScCadenceEstimator* e, double bin_width_s);
void sc_cadence_observe(ScCadenceEstimator* e, int64_t ts_s);
void sc_cadence_estimate(const ScCadenceEstimator* e, ScCadenceEstimate* out);

#endif /* SC_CADENCE_H */
