/* test_knn.c — gated k-NN classification (System §6). */
#include <string.h>

#include "sc_knn.h"
#include "sc_test.h"

static ScFeatureVector fv_make(
    int32_t bin,
    ScModulation mod,
    int32_t s0,
    int32_t s1,
    int32_t nsym,
    int32_t bitrate,
    int32_t pre,
    int32_t rep) {
    ScFeatureVector fv;
    memset(&fv, 0, sizeof(fv));
    fv.freq_bin = bin;
    fv.modulation = mod;
    fv.sym_dur_us[0] = s0;
    fv.sym_dur_us[1] = s1;
    fv.n_sym_dur = 2;
    fv.n_symbols = nsym;
    fv.est_bitrate = bitrate;
    fv.preamble_len = pre;
    fv.repeat_count = rep;
    return fv;
}

int main(void) {
    printf("test_knn\n");

    const int32_t BIN = 433920000;

    ScFingerprint cands[4];
    /* A: close to the query */
    cands[0].fv = fv_make(BIN, SC_MOD_OOK, 300, 900, 45, 3333, 8, 3);
    cands[0].cadence_class = SC_CADENCE_PERIODIC;
    cands[0].period_s = 60;
    cands[0].device_name = "Acurite weather";
    cands[0].device_class = 3;
    /* B: same gate, different timing */
    cands[1].fv = fv_make(BIN, SC_MOD_OOK, 200, 600, 20, 5000, 2, 1);
    cands[1].cadence_class = SC_CADENCE_NONE;
    cands[1].period_s = 0;
    cands[1].device_name = "PT2262 remote";
    cands[1].device_class = 8;
    /* C: gated OUT by modulation */
    cands[2].fv = fv_make(BIN, SC_MOD_2FSK, 300, 900, 45, 3333, 8, 3);
    cands[2].cadence_class = SC_CADENCE_NONE;
    cands[2].period_s = 0;
    cands[2].device_name = "TPMS";
    cands[2].device_class = 2;
    /* D: gated OUT by freq_bin */
    cands[3].fv = fv_make(315000000, SC_MOD_OOK, 300, 900, 45, 3333, 8, 3);
    cands[3].cadence_class = SC_CADENCE_NONE;
    cands[3].period_s = 0;
    cands[3].device_name = "other-band";
    cands[3].device_class = 8;

    /* query: near A, no cadence yet */
    ScKnnQuery q;
    q.fv = fv_make(BIN, SC_MOD_OOK, 305, 890, 44, 3300, 8, 3);
    q.cadence_class = SC_CADENCE_NONE;
    q.period_s = 0;

    ScKnnMatch m[4];
    size_t k = sc_knn_match(&q, cands, 4, m, 4);
    /* only A and B survive the gate */
    SC_CHECK_INT(k, 2);
    SC_CHECK_INT(m[0].index, 0); /* A is closest */
    SC_CHECK_INT(m[1].index, 1);
    SC_CHECK(m[0].confidence > m[1].confidence, "A ranks above B");
    SC_CHECK(m[0].confidence > 0.8, "near-identical match is high confidence");

    /* cadence agreement boosts A's confidence (soft, System §6) */
    ScKnnQuery qc = q;
    qc.cadence_class = SC_CADENCE_PERIODIC;
    qc.period_s = 61;
    ScKnnMatch mc[4];
    sc_knn_match(&qc, cands, 4, mc, 4);
    SC_CHECK(mc[0].confidence >= m[0].confidence, "matching cadence boosts confidence");

    /* cadence disagreement penalizes */
    ScKnnQuery qd = q;
    qd.cadence_class = SC_CADENCE_EVENT_DRIVEN;
    qd.period_s = 0;
    ScKnnMatch md[4];
    sc_knn_match(&qd, cands, 4, md, 4);
    SC_CHECK(md[0].confidence < m[0].confidence, "mismatched cadence lowers confidence");

    return sc_test_summary();
}
