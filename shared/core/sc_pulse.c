#include "sc_pulse.h"

#include <stdlib.h>

#include "sc_util.h"

#define SC_PULSE_KMAX      8 /* internal provisional-cluster slots */
#define SC_PULSE_ABS_FLOOR 20 /* min absolute tolerance (us) so tiny widths don't over-split */

typedef struct {
    float sum;
    int32_t count;
    int32_t center;
} Slot;

size_t sc_pulse_cluster(
    const int32_t* timings,
    size_t n,
    float rel_tol,
    ScPulseCluster* out,
    size_t max_clusters) {
    if(!timings || !out || max_clusters == 0) return 0;

    Slot slots[SC_PULSE_KMAX];
    size_t nslots = 0;

    for(size_t i = 0; i < n; i++) {
        int32_t w = sc_iabs32(timings[i]);
        if(w == 0) continue;

        /* find best matching slot */
        int best = -1;
        int32_t best_d = 0;
        for(size_t s = 0; s < nslots; s++) {
            int32_t tol = (int32_t)(rel_tol * slots[s].center);
            if(tol < SC_PULSE_ABS_FLOOR) tol = SC_PULSE_ABS_FLOOR;
            int32_t d = sc_iabs32(w - slots[s].center);
            if(d <= tol && (best < 0 || d < best_d)) {
                best = (int)s;
                best_d = d;
            }
        }

        if(best < 0) {
            if(nslots < SC_PULSE_KMAX) {
                slots[nslots].sum = w;
                slots[nslots].count = 1;
                slots[nslots].center = w;
                nslots++;
                continue;
            }
            /* no slot free: assign to nearest existing slot */
            int32_t nd = 0;
            for(size_t s = 0; s < nslots; s++) {
                int32_t d = sc_iabs32(w - slots[s].center);
                if(best < 0 || d < nd) {
                    best = (int)s;
                    nd = d;
                }
            }
        }
        slots[best].sum += w;
        slots[best].count++;
        slots[best].center = (int32_t)(slots[best].sum / slots[best].count);
    }

    /* sort slots by count descending (simple selection sort; nslots <= 8) */
    for(size_t a = 0; a < nslots; a++) {
        size_t max = a;
        for(size_t b = a + 1; b < nslots; b++) {
            if(slots[b].count > slots[max].count) max = b;
        }
        if(max != a) {
            Slot tmp = slots[a];
            slots[a] = slots[max];
            slots[max] = tmp;
        }
    }

    size_t out_n = nslots < max_clusters ? nslots : max_clusters;
    for(size_t i = 0; i < out_n; i++) {
        out[i].center_us = slots[i].center;
        out[i].count = slots[i].count;
    }
    return out_n;
}
