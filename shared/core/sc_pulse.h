/* sc_pulse.h — pulse-width histogram + symbol clustering (System §7).
 *
 * Clusters the absolute durations of a RAW timing stream into a few dominant symbol
 * widths (the raw material for sym_dur_us[1..3]). Allocation-free and O(n*K) so it runs
 * on-device per capture (Zero §5.5: "keep it cheap enough to run on-device").
 */
#ifndef SC_PULSE_H
#define SC_PULSE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t center_us; /* representative (mean) width, microseconds */
    int32_t count;     /* how many pulses fell in this cluster */
} ScPulseCluster;

/* Cluster absolute pulse widths from signed `timings` (positive=mark, negative=space;
 * both contribute their magnitude). Writes up to `max_clusters` clusters sorted by count
 * descending into `out`; returns the number written. `rel_tol` is the relative width
 * tolerance for grouping (e.g. 0.25 = within 25%). Durations of 0 are ignored. */
size_t sc_pulse_cluster(
    const int32_t* timings,
    size_t n,
    double rel_tol,
    ScPulseCluster* out,
    size_t max_clusters);

#endif /* SC_PULSE_H */
