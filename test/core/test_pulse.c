/* test_pulse.c — pulse-width clustering (System §7). */
#include "sc_pulse.h"
#include "sc_test.h"

int main(void) {
    printf("test_pulse\n");

    /* Two dominant widths: 300 (x6) and 900 (x3), signs alternate. */
    int32_t t[] = {
        300,
        -300,
        300,
        -300,
        300,
        -300, /* six 300s */
        900,
        -900,
        900, /* three 900s */
    };
    ScPulseCluster c[3];
    size_t nc = sc_pulse_cluster(t, sizeof(t) / sizeof(t[0]), 0.25, c, 3);
    SC_CHECK_INT(nc, 2);
    /* sorted by count desc: 300 first (6), 900 second (3) */
    SC_CHECK_INT(c[0].count, 6);
    SC_CHECK_DBL(c[0].center_us, 300, 15);
    SC_CHECK_INT(c[1].count, 3);
    SC_CHECK_DBL(c[1].center_us, 900, 45);

    /* zeros ignored; jitter within tolerance stays one cluster */
    int32_t j[] = {0, 310, -290, 305, -295, 0, 300};
    ScPulseCluster jc[3];
    size_t jn = sc_pulse_cluster(j, sizeof(j) / sizeof(j[0]), 0.25, jc, 3);
    SC_CHECK_INT(jn, 1);
    SC_CHECK_INT(jc[0].count, 5);

    return sc_test_summary();
}
