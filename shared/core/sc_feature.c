#include "sc_feature.h"

#include <string.h>

#include "sc_pulse.h"

#define SC_FEATURE_REL_TOL 0.25
#define SC_FEATURE_GAP_FACTOR 5 /* a gap > factor*largest-symbol separates repeats */

static int32_t iabs32(int32_t v) {
    return v < 0 ? -v : v;
}

ScResult sc_feature_compute(
    const int32_t* timings,
    size_t n,
    int32_t freq_hz,
    ScModulation mod,
    ScFeatureVector* out) {
    if(!out) return SC_ERR;
    memset(out, 0, sizeof(*out));
    out->freq_bin = sc_freq_bin(freq_hz);
    out->modulation = mod;
    if(!timings || n == 0) return SC_EMPTY;

    /* n_symbols + widest pulse */
    int32_t nsym = 0;
    for(size_t i = 0; i < n; i++) {
        if(iabs32(timings[i]) != 0) nsym++;
    }
    out->n_symbols = nsym;
    if(nsym == 0) return SC_EMPTY;

    /* dominant symbol durations (top-3 by frequency), then sorted ascending */
    ScPulseCluster c[3];
    size_t nc = sc_pulse_cluster(timings, n, SC_FEATURE_REL_TOL, c, 3);
    /* modal (most common) symbol — clusters arrive count-descending, so c[0] is the
     * mode. Capture it before we re-sort ascending; the repeat-gap threshold is a
     * multiple of the base symbol, NOT of the largest cluster (which may be the gap). */
    int32_t mode_sym = (nc > 0) ? c[0].center_us : 0;
    /* selection sort ascending by center */
    for(size_t a = 0; a < nc; a++) {
        size_t min = a;
        for(size_t b = a + 1; b < nc; b++) {
            if(c[b].center_us < c[min].center_us) min = b;
        }
        if(min != a) {
            ScPulseCluster t = c[a];
            c[a] = c[min];
            c[min] = t;
        }
    }
    out->n_sym_dur = (int32_t)nc;
    for(size_t i = 0; i < nc; i++) {
        out->sym_dur_us[i] = c[i].center_us;
    }

    /* est_bitrate from the shortest dominant symbol */
    if(nc > 0 && out->sym_dur_us[0] > 0) {
        out->est_bitrate = (int32_t)(1000000.0 / (double)out->sym_dur_us[0] + 0.5);
    }

    /* preamble: leading run of consecutive non-zero pulses within tol of the first */
    int32_t ref = 0;
    size_t start = 0;
    while(start < n && iabs32(timings[start]) == 0) start++;
    if(start < n) {
        ref = iabs32(timings[start]);
        int32_t tol = (int32_t)(SC_FEATURE_REL_TOL * ref);
        if(tol < 20) tol = 20;
        int32_t run = 0;
        for(size_t i = start; i < n; i++) {
            int32_t w = iabs32(timings[i]);
            if(w == 0) continue;
            if(iabs32(w - ref) <= tol)
                run++;
            else
                break;
        }
        out->preamble_len = run;
    }

    /* repeat_count: interior long gaps (excluding the trailing element) + 1.
     * A separator gap is a LOW period several times the base (modal) symbol. */
    int32_t gap_threshold = mode_sym * SC_FEATURE_GAP_FACTOR;
    if(gap_threshold <= 0) gap_threshold = 1;
    int32_t interior_gaps = 0;
    for(size_t i = 0; i + 1 < n; i++) {
        if(timings[i] < 0 && iabs32(timings[i]) > gap_threshold) interior_gaps++;
    }
    out->repeat_count = interior_gaps + 1;

    return SC_OK;
}
