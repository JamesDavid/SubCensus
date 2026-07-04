/* test_feature.c — canonical feature vector (System §7). */
#include <string.h>

#include "sc_feature.h"
#include "sc_test.h"

/* Build 3 repeats of a synthetic OOK frame: 8-pulse preamble (|300|), a data body
 * mixing 300/900, and a long inter-frame gap. */
static size_t build_frames(int32_t* buf) {
    static const int32_t frame[] = {
        300,
        -300,
        300,
        -300,
        300,
        -300,
        300,
        -300, /* preamble: 8 x |300| */
        900,
        -300,
        300,
        -900,
        900,
        -300, /* data: 3x300, 3x900 */
    };
    static const int32_t gap = -8850;
    size_t n = 0;
    for(int rep = 0; rep < 3; rep++) {
        for(size_t i = 0; i < sizeof(frame) / sizeof(frame[0]); i++)
            buf[n++] = frame[i];
        buf[n++] = gap;
    }
    return n; /* 3 * (14 + 1) = 45 */
}

int main(void) {
    printf("test_feature\n");

    int32_t buf[64];
    size_t n = build_frames(buf);
    SC_CHECK_INT(n, 45);

    ScFeatureVector fv;
    ScResult r = sc_feature_compute(buf, n, 433918000, SC_MOD_OOK, &fv);
    SC_CHECK_INT(r, SC_OK);

    /* freq binned to nearest 5 kHz */
    SC_CHECK_INT(fv.freq_bin, 433920000);
    SC_CHECK_INT(fv.modulation, SC_MOD_OOK);
    SC_CHECK_INT(fv.n_symbols, 45);

    /* dominant symbols ascending: ~300 then ~900 (gap 8850 is the 3rd) */
    SC_CHECK_INT(fv.n_sym_dur, 3);
    SC_CHECK_DBL(fv.sym_dur_us[0], 300, 20);
    SC_CHECK_DBL(fv.sym_dur_us[1], 900, 60);
    SC_CHECK(fv.sym_dur_us[0] < fv.sym_dur_us[1], "sym durations ascending");

    /* est_bitrate ~ 1e6/300 */
    SC_CHECK_DBL(fv.est_bitrate, 3333, 60);

    /* preamble = 8 equal leading pulses */
    SC_CHECK_INT(fv.preamble_len, 8);

    /* 3 frames separated by 2 interior gaps -> repeat_count 3 */
    SC_CHECK_INT(fv.repeat_count, 3);

    /* empty input still sets freq_bin/modulation */
    ScFeatureVector empty;
    r = sc_feature_compute(NULL, 0, 315000000, SC_MOD_2FSK, &empty);
    SC_CHECK_INT(r, SC_EMPTY);
    SC_CHECK_INT(empty.freq_bin, 315000000);
    SC_CHECK_INT(empty.modulation, SC_MOD_2FSK);

    return sc_test_summary();
}
