#include "sc_cadence.h"

#include <math.h>
#include <string.h>

/* Classification thresholds (heuristic; System §7a describes the shape). */
#define SC_NEAR_CONT_S      2.0 /* mean interval below this => near-continuous */
#define SC_SUPPORT_PERIODIC 0.75 /* fraction of intervals on a fundamental multiple */
#define SC_FOLD_TOL         0.18 /* |residual|/p under this counts as "on a multiple" */
#define SC_PERIODIC_TIGHT   0.6 /* folded tightness for periodic vs quasi-periodic */
#define SC_MAX_HARMONIC     6

/* Fold interval x onto fundamental p; return normalized residual 0..0.5. */
static float fold_residual(float x, float p) {
    if(p <= 0) return 0.5;
    float m = floorf(x / p + 0.5);
    if(m < 1) m = 1;
    if(m > SC_MAX_HARMONIC) m = SC_MAX_HARMONIC;
    float r = fabsf(x - m * p) / p;
    return r > 0.5 ? 0.5 : r;
}

/* Core classifier over an explicit interval list. */
static void classify_intervals(const float* iv, size_t n, ScCadenceEstimate* out) {
    memset(out, 0, sizeof(*out));
    out->cls = SC_CADENCE_SEEN_ONCE;
    if(n == 0) return;
    out->samples = (int32_t)n;

    float sum = 0.0, sq = 0.0, mn = iv[0];
    for(size_t i = 0; i < n; i++) {
        sum += iv[i];
        sq += iv[i] * iv[i];
        if(iv[i] < mn) mn = iv[i];
    }
    float mean = sum / (float)n;
    float var = sq / (float)n - mean * mean;
    if(var < 0) var = 0;
    float sd = sqrtf(var);
    float cov = mean > 0 ? sd / mean : 1.0;
    float regularity = 1.0 - (cov < 1.0 ? cov : 1.0);
    out->regularity = regularity;

    if(mean > 0 && mean < SC_NEAR_CONT_S) {
        out->cls = SC_CADENCE_NEAR_CONTINUOUS;
        out->period_s = mean;
        return;
    }

    /* fundamental = smoothed smallest cluster (mean of intervals within 25% of the min) */
    float lo = mn * 0.75, hi = mn * 1.25;
    float psum = 0.0;
    int pcount = 0;
    for(size_t i = 0; i < n; i++) {
        if(iv[i] >= lo && iv[i] <= hi) {
            psum += iv[i];
            pcount++;
        }
    }
    float p = pcount > 0 ? psum / pcount : mn;

    /* support + folded tightness */
    int on_mult = 0;
    float resid_sum = 0.0;
    for(size_t i = 0; i < n; i++) {
        float r = fold_residual(iv[i], p);
        resid_sum += r;
        if(r <= SC_FOLD_TOL) on_mult++;
    }
    float support = (float)on_mult / (float)n;
    float mean_resid = resid_sum / (float)n;
    float folded_tight = 1.0 - (mean_resid / 0.25 < 1.0 ? mean_resid / 0.25 : 1.0);

    if(support >= SC_SUPPORT_PERIODIC) {
        out->cls = (folded_tight >= SC_PERIODIC_TIGHT) ? SC_CADENCE_PERIODIC :
                                                         SC_CADENCE_QUASI_PERIODIC;
        out->period_s = p;
    } else {
        out->cls = SC_CADENCE_EVENT_DRIVEN;
        out->period_s = 0.0; /* null for event-driven (System §7a) */
    }
}

void sc_cadence_from_timestamps(const int64_t* ts_s, size_t n, ScCadenceEstimate* out) {
    if(n <= 1) {
        memset(out, 0, sizeof(*out));
        out->cls = SC_CADENCE_SEEN_ONCE;
        return;
    }
    /* copy + insertion sort (n expected small for a single device's history window) */
    int64_t sorted[512];
    size_t m = n < 512 ? n : 512;
    for(size_t i = 0; i < m; i++)
        sorted[i] = ts_s[i];
    for(size_t i = 1; i < m; i++) {
        int64_t v = sorted[i];
        size_t j = i;
        while(j > 0 && sorted[j - 1] > v) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = v;
    }
    float iv[512];
    size_t nv = 0;
    for(size_t i = 1; i < m; i++) {
        float d = (float)(sorted[i] - sorted[i - 1]);
        if(d > 0) iv[nv++] = d;
    }
    classify_intervals(iv, nv, out);
}

/* --- running estimator --- */

void sc_cadence_init(ScCadenceEstimator* e, float bin_width_s) {
    memset(e, 0, sizeof(*e));
    e->bin_width_s = bin_width_s > 0 ? bin_width_s : 1.0;
}

void sc_cadence_observe(ScCadenceEstimator* e, int64_t ts_s) {
    if(e->count > 0) {
        float d = (float)(ts_s - e->last_ts);
        if(d > 0) {
            e->interval_sum += d;
            e->interval_sqsum += d * d;
            e->n_intervals++;
            int bin = (int)(d / e->bin_width_s);
            if(bin < 0) bin = 0;
            if(bin >= SC_CADENCE_HIST_BINS) bin = SC_CADENCE_HIST_BINS - 1;
            e->hist[bin]++;
        }
    }
    e->last_ts = ts_s;
    e->count++;
}

void sc_cadence_estimate(const ScCadenceEstimator* e, ScCadenceEstimate* out) {
    memset(out, 0, sizeof(*out));
    if(e->n_intervals == 0) {
        out->cls = SC_CADENCE_SEEN_ONCE;
        return;
    }
    out->samples = e->n_intervals;
    float mean = e->interval_sum / e->n_intervals;
    float var = e->interval_sqsum / e->n_intervals - mean * mean;
    if(var < 0) var = 0;
    float cov = mean > 0 ? sqrtf(var) / mean : 1.0;
    out->regularity = 1.0 - (cov < 1.0 ? cov : 1.0);

    if(mean > 0 && mean < SC_NEAR_CONT_S) {
        out->cls = SC_CADENCE_NEAR_CONTINUOUS;
        out->period_s = mean;
        return;
    }

    /* fundamental = smallest histogram bin with meaningful support */
    int min_support = e->n_intervals / 6;
    if(min_support < 2) min_support = 2;
    int fund_bin = -1;
    for(int b = 0; b < SC_CADENCE_HIST_BINS; b++) {
        if(e->hist[b] >= min_support) {
            fund_bin = b;
            break;
        }
    }
    if(fund_bin < 0) {
        /* no supported cluster -> arrivals are spread out => event-driven */
        out->cls = SC_CADENCE_EVENT_DRIVEN;
        out->period_s = 0.0;
        return;
    }
    float p = (fund_bin + 0.5) * e->bin_width_s;

    /* support: fraction of intervals whose bin center folds onto a multiple of p */
    int on_mult = 0, total = 0;
    float resid_sum = 0.0;
    for(int b = 0; b < SC_CADENCE_HIST_BINS; b++) {
        if(e->hist[b] == 0) continue;
        float x = (b + 0.5) * e->bin_width_s;
        float r = fold_residual(x, p);
        on_mult += (r <= SC_FOLD_TOL) ? e->hist[b] : 0;
        resid_sum += r * e->hist[b];
        total += e->hist[b];
    }
    float support = total > 0 ? (float)on_mult / total : 0.0;
    float mean_resid = total > 0 ? resid_sum / total : 0.5;
    float folded_tight = 1.0 - (mean_resid / 0.25 < 1.0 ? mean_resid / 0.25 : 1.0);

    if(support >= SC_SUPPORT_PERIODIC) {
        out->cls = (folded_tight >= SC_PERIODIC_TIGHT) ? SC_CADENCE_PERIODIC :
                                                         SC_CADENCE_QUASI_PERIODIC;
        out->period_s = p;
    } else {
        out->cls = SC_CADENCE_EVENT_DRIVEN;
        out->period_s = 0.0;
    }
}
