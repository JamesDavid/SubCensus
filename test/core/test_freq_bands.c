/* test_freq_bands.c — CC1101 legal-segment predicate (Zero §3.3 / §7). */
#include "sc_freq_bands.h"
#include "sc_test.h"

int main(void) {
    printf("test_freq_bands\n");

    /* in-band US/EU presets */
    SC_CHECK(sc_freq_in_cc1101_band(315000000), "315 MHz in band");
    SC_CHECK(sc_freq_in_cc1101_band(390000000), "390 MHz in band");
    SC_CHECK(sc_freq_in_cc1101_band(433920000), "433.92 MHz in band");
    SC_CHECK(sc_freq_in_cc1101_band(868350000), "868.35 MHz in band");
    SC_CHECK(sc_freq_in_cc1101_band(915000000), "915 MHz in band");

    /* segment edges */
    SC_CHECK(sc_freq_in_cc1101_band(300000000), "300 lower edge");
    SC_CHECK(sc_freq_in_cc1101_band(348000000), "348 upper edge");
    SC_CHECK(sc_freq_in_cc1101_band(928000000), "928 upper edge");

    /* out of band -> rejected (Zero §7 allowed-frequency guard) */
    SC_CHECK(!sc_freq_in_cc1101_band(200000000), "200 MHz rejected");
    SC_CHECK(!sc_freq_in_cc1101_band(360000000), "360 MHz gap rejected");
    SC_CHECK(!sc_freq_in_cc1101_band(500000000), "500 MHz gap rejected");
    SC_CHECK(!sc_freq_in_cc1101_band(1000000000), "1 GHz rejected");

    return sc_test_summary();
}
