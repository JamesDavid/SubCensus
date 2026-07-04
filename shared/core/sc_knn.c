#include "sc_knn.h"

#include <math.h>

/* Fixed normalization scales + weights (identical on every sensor — System §7). */
#define KNN_SCALE_SYM 300.0
#define KNN_SCALE_NSYM 50.0
#define KNN_SCALE_BITRATE 2000.0
#define KNN_SCALE_PREAMBLE 8.0
#define KNN_SCALE_REPEAT 3.0

#define KNN_W_SYM 1.0
#define KNN_W_NSYM 0.3
#define KNN_W_BITRATE 0.5
#define KNN_W_PREAMBLE 0.5
#define KNN_W_REPEAT 0.3

#define KNN_MISSING_PENALTY 1.0 /* squared term added when one side lacks a sym duration */

/* Cadence soft adjustment (System §6). */
#define KNN_CADENCE_AGREE 1.15
#define KNN_CADENCE_PERIOD_AGREE 1.10
#define KNN_CADENCE_DISAGREE 0.80
#define KNN_PERIOD_TOL 0.20

static double sq(double x) {
    return x * x;
}

static double term(double a, double b, double scale, double w) {
    return w * sq((a - b) / scale);
}

static double gated_distance(const ScFeatureVector* a, const ScFeatureVector* b) {
    double d2 = 0.0;

    /* symbol durations: compare where both present; penalize count mismatch */
    for(int i = 0; i < 3; i++) {
        int ha = i < a->n_sym_dur;
        int hb = i < b->n_sym_dur;
        if(ha && hb) {
            d2 += term(a->sym_dur_us[i], b->sym_dur_us[i], KNN_SCALE_SYM, KNN_W_SYM);
        } else if(ha != hb) {
            d2 += KNN_W_SYM * KNN_MISSING_PENALTY;
        }
    }
    d2 += term(a->n_symbols, b->n_symbols, KNN_SCALE_NSYM, KNN_W_NSYM);
    d2 += term(a->est_bitrate, b->est_bitrate, KNN_SCALE_BITRATE, KNN_W_BITRATE);
    d2 += term(a->preamble_len, b->preamble_len, KNN_SCALE_PREAMBLE, KNN_W_PREAMBLE);
    d2 += term(a->repeat_count, b->repeat_count, KNN_SCALE_REPEAT, KNN_W_REPEAT);
    return sqrt(d2);
}

static double cadence_adjust(
    double conf, ScCadenceClass qc, double qp, ScCadenceClass cc, double cp) {
    if(qc == SC_CADENCE_NONE || cc == SC_CADENCE_NONE) return conf; /* soft: no data, no change */
    if(qc == cc) {
        conf *= KNN_CADENCE_AGREE;
        if(qp > 0 && cp > 0) {
            double rel = fabs(qp - cp) / (cp > qp ? cp : qp);
            if(rel < KNN_PERIOD_TOL) conf *= KNN_CADENCE_PERIOD_AGREE;
        }
    } else {
        conf *= KNN_CADENCE_DISAGREE;
    }
    if(conf > 1.0) conf = 1.0;
    if(conf < 0.0) conf = 0.0;
    return conf;
}

size_t sc_knn_match(
    const ScKnnQuery* q,
    const ScFingerprint* cands,
    size_t n,
    ScKnnMatch* out,
    size_t topn) {
    if(!q || !cands || !out || topn == 0) return 0;

    size_t count = 0;
    for(size_t i = 0; i < n; i++) {
        /* hard gate: freq_bin + modulation (System §6) */
        if(cands[i].fv.freq_bin != q->fv.freq_bin) continue;
        if(cands[i].fv.modulation != q->fv.modulation) continue;

        double d = gated_distance(&q->fv, &cands[i].fv);
        double conf = 1.0 / (1.0 + d);
        conf = cadence_adjust(
            conf, q->cadence_class, q->period_s, cands[i].cadence_class, cands[i].period_s);

        ScKnnMatch m = {(int)i, d, conf};

        /* insertion into the top-N (sorted by confidence desc) */
        size_t limit = count < topn ? count : topn;
        size_t pos = limit;
        while(pos > 0 && out[pos - 1].confidence < m.confidence) pos--;
        if(pos < topn) {
            size_t last = (count < topn) ? count : topn - 1;
            for(size_t j = last; j > pos; j--) out[j] = out[j - 1];
            out[pos] = m;
            if(count < topn) count++;
        }
    }
    return count;
}
