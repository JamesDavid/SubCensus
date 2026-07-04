/* test_esp_capture.c — RMT edge-capture decode + feature vector (Esp §3, System §7). */
#include "esp_capture.h"
#include "sc_feature.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_capture\n");

    /* 1 tick = 1 us; a small OOK-ish burst: 350 high, 350 low, 1050 high, 350 low. */
    ScRmtItem items[] = {
        {350, 1, 350, 0},
        {1050, 1, 350, 0},
        {0, 0, 0, 0}, /* end-of-burst */
    };
    int32_t t[16];
    size_t n = esp_capture_rmt_to_timings(items, 3, 1.0f, t, 16);
    SC_CHECK_INT(n, 4);
    SC_CHECK_INT(t[0], 350);   /* high -> + */
    SC_CHECK_INT(t[1], -350);  /* low  -> - */
    SC_CHECK_INT(t[2], 1050);
    SC_CHECK_INT(t[3], -350);

    /* ticks_per_us scaling: 2 ticks/us halves the durations */
    int32_t t2[16];
    size_t n2 = esp_capture_rmt_to_timings(items, 3, 2.0f, t2, 16);
    SC_CHECK_INT(n2, 4);
    SC_CHECK_INT(t2[0], 175);

    /* the decoded timings feed the SAME shared feature vector as the Zero (System §7) */
    ScRmtItem frame[64];
    size_t fi = 0;
    for(int rep = 0; rep < 3; rep++) {
        for(int p = 0; p < 4; p++) frame[fi++] = (ScRmtItem){300, 1, 300, 0}; /* preamble */
        frame[fi++] = (ScRmtItem){900, 1, 300, 0};
        frame[fi++] = (ScRmtItem){300, 1, 8850, 0}; /* data + long inter-frame gap */
    }
    frame[fi++] = (ScRmtItem){0, 0, 0, 0};
    int32_t timings[256];
    size_t tn = esp_capture_rmt_to_timings(frame, fi, 1.0f, timings, 256);
    ScFeatureVector fv;
    ScResult r = sc_feature_compute(timings, tn, 433920000, SC_MOD_OOK, &fv);
    SC_CHECK_INT(r, SC_OK);
    SC_CHECK_INT(fv.freq_bin, 433920000);
    SC_CHECK_INT(fv.repeat_count, 3); /* 3 frames separated by inter-frame gaps */

    return sc_test_summary();
}
