/* test_cadence.c — cadence / temporal fingerprint (System §7a). */
#include "sc_cadence.h"
#include "sc_test.h"

int main(void) {
    printf("test_cadence\n");
    ScCadenceEstimate est;

    /* seen-once */
    int64_t one[] = {1000};
    sc_cadence_from_timestamps(one, 1, &est);
    SC_CHECK_INT(est.cls, SC_CADENCE_SEEN_ONCE);
    SC_CHECK_INT(est.samples, 0);

    /* metronomic 60 s -> periodic, period ~60, high regularity */
    int64_t per[] = {0, 60, 120, 180, 240, 300};
    sc_cadence_from_timestamps(per, 6, &est);
    SC_CHECK_INT(est.cls, SC_CADENCE_PERIODIC);
    SC_CHECK_DBL(est.period_s, 60, 2);
    SC_CHECK(est.regularity > 0.9, "metronomic regularity high");

    /* dropout-robust: a missed reception makes one interval 120 (2x). Fundamental must
     * still resolve to ~60 and stay periodic (System §7a). */
    int64_t drop[] = {0, 60, 180, 240, 300, 420};
    sc_cadence_from_timestamps(drop, 6, &est);
    SC_CHECK_DBL(est.period_s, 60, 5);
    SC_CHECK(
        est.cls == SC_CADENCE_PERIODIC || est.cls == SC_CADENCE_QUASI_PERIODIC,
        "dropout stays (quasi-)periodic on the fundamental");

    /* event-driven: irregular, non-harmonic arrivals */
    int64_t evt[] = {0, 5, 7, 100, 103, 500, 505};
    sc_cadence_from_timestamps(evt, 7, &est);
    SC_CHECK_INT(est.cls, SC_CADENCE_EVENT_DRIVEN);
    SC_CHECK_DBL(est.period_s, 0, 0.0001); /* null for event-driven */

    /* near-continuous: sub-2s intervals */
    int64_t cont[] = {0, 1, 2, 3, 4, 5};
    sc_cadence_from_timestamps(cont, 6, &est);
    SC_CHECK_INT(est.cls, SC_CADENCE_NEAR_CONTINUOUS);

    /* running estimator agrees on the periodic case */
    ScCadenceEstimator e;
    sc_cadence_init(&e, 10.0); /* 10 s bins */
    for(int i = 0; i < 8; i++)
        sc_cadence_observe(&e, (int64_t)i * 60);
    sc_cadence_estimate(&e, &est);
    SC_CHECK(
        est.cls == SC_CADENCE_PERIODIC || est.cls == SC_CADENCE_QUASI_PERIODIC,
        "running estimator finds periodic");
    SC_CHECK_DBL(est.period_s, 60, 12); /* within a bin width */
    SC_CHECK_INT(est.samples, 7);

    return sc_test_summary();
}
