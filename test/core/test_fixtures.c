/* test_fixtures.c — drive a real .sub fixture file through the pipeline (Debug §1.2).
 *
 * Loads a `.sub` fixture from disk (SC_FIXTURES_DIR, injected by run_tests.py), parses it,
 * and computes the feature vector — asserting golden values for a fixture with known
 * synthetic structure. Fixtures prove the processing path (RF boundary, Debug §7).
 */
#include <stdio.h>
#include <stdlib.h>

#include "sc_feature.h"
#include "sc_sub.h"
#include "sc_test.h"

#ifndef SC_FIXTURES_DIR
#define SC_FIXTURES_DIR "test/fixtures"
#endif

static size_t read_file(const char* path, char* buf, size_t cap) {
    FILE* f = fopen(path, "rb");
    if(!f) return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

int main(void) {
    printf("test_fixtures\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/sub/synth_ook_remote_433.sub", SC_FIXTURES_DIR);

    char text[8192];
    size_t len = read_file(path, text, sizeof(text));
    SC_CHECK(len > 0, "fixture .sub loaded from disk");

    ScSubMeta meta;
    int32_t timings[512];
    size_t n = 0;
    ScResult r = sc_sub_parse(text, len, &meta, timings, 512, &n);
    SC_CHECK_INT(r, SC_OK);
    SC_CHECK_INT(meta.frequency, 433920000);
    SC_CHECK(n > 100, "parsed the full 5-frame stream");

    ScFeatureVector fv;
    r = sc_feature_compute(timings, n, meta.frequency, SC_MOD_OOK, &fv);
    SC_CHECK_INT(r, SC_OK);

    /* golden feature values for this synthetic PT2262-style remote */
    SC_CHECK_INT(fv.freq_bin, 433920000);
    SC_CHECK_INT(fv.modulation, SC_MOD_OOK);
    SC_CHECK_INT(fv.repeat_count, 5);  /* 5 repeated frames => 4 interior sync gaps + 1 */
    SC_CHECK_INT(fv.preamble_len, 8);  /* 8-pulse |350| preamble */
    SC_CHECK_INT(fv.n_sym_dur, 3);
    SC_CHECK_DBL(fv.sym_dur_us[0], 350, 30);   /* short */
    SC_CHECK_DBL(fv.sym_dur_us[1], 1050, 90);  /* long */
    SC_CHECK(fv.est_bitrate > 2500 && fv.est_bitrate < 3200, "est_bitrate ~ 1e6/350");

    return sc_test_summary();
}
