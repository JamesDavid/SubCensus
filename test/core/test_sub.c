/* test_sub.c — .sub RAW parse/encode round-trip (Zero §5.1, §5.4). */
#include "sc_sub.h"
#include "sc_test.h"

static const char SAMPLE[] = "Filetype: Flipper SubGhz RAW File\n"
                             "Version: 1\n"
                             "Frequency: 433920000\n"
                             "Preset: FuriHalSubGhzPresetOok650Async\n"
                             "Protocol: RAW\n"
                             "RAW_Data: 291 -100 300 -8850 291 -100\n"
                             "RAW_Data: 300 -200\n";

int main(void) {
    printf("test_sub\n");

    ScSubMeta meta;
    int32_t t[32];
    size_t n = 0;
    ScResult r = sc_sub_parse(SAMPLE, sizeof(SAMPLE) - 1, &meta, t, 32, &n);
    SC_CHECK_INT(r, SC_OK);
    SC_CHECK_INT(meta.frequency, 433920000);
    SC_CHECK_STR(meta.preset, "FuriHalSubGhzPresetOok650Async");
    SC_CHECK_STR(meta.protocol, "RAW");
    SC_CHECK_INT(n, 8);
    SC_CHECK_INT(t[0], 291);
    SC_CHECK_INT(t[1], -100);
    SC_CHECK_INT(t[3], -8850);
    SC_CHECK_INT(t[6], 300);
    SC_CHECK_INT(t[7], -200);

    /* Round-trip: encode then parse back, timings must match exactly. */
    char out[512];
    size_t out_len = 0;
    r = sc_sub_encode(&meta, t, n, out, sizeof(out), 4, &out_len);
    SC_CHECK_INT(r, SC_OK);
    SC_CHECK(out_len > 0, "encode produced output");

    ScSubMeta meta2;
    int32_t t2[32];
    size_t n2 = 0;
    r = sc_sub_parse(out, out_len, &meta2, t2, 32, &n2);
    SC_CHECK_INT(r, SC_OK);
    SC_CHECK_INT(meta2.frequency, 433920000);
    SC_CHECK_INT(n2, n);
    for(size_t i = 0; i < n; i++)
        SC_CHECK_INT(t2[i], t[i]);

    /* Truncation: capacity smaller than timing count. */
    int32_t small[3];
    size_t ns = 0;
    r = sc_sub_parse(SAMPLE, sizeof(SAMPLE) - 1, NULL, small, 3, &ns);
    SC_CHECK_INT(r, SC_TRUNCATED);
    SC_CHECK_INT(ns, 3);

    /* Encode overflow: tiny buffer. */
    char tiny[16];
    size_t tl = 0;
    r = sc_sub_encode(&meta, t, n, tiny, sizeof(tiny), 0, &tl);
    SC_CHECK_INT(r, SC_TRUNCATED);

    return sc_test_summary();
}
