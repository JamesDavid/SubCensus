/* test_esp_tx.c — TX guard (Esp §3, System §7b). */
#include "esp_tx.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_tx\n");

    /* off by default: no TX even on a legal frequency */
    SC_CHECK(!esp_tx_allowed(433920000, false), "tx disabled -> not allowed");

    /* enabled + in-band -> allowed */
    SC_CHECK(esp_tx_allowed(433920000, true), "433.92 allowed when enabled");
    SC_CHECK(esp_tx_allowed(315000000, true), "315 allowed");
    SC_CHECK(esp_tx_allowed(915000000, true), "915 allowed");

    /* enabled but out of the CC1101 legal segments -> blocked */
    SC_CHECK(!esp_tx_allowed(500000000, true), "500 MHz gap blocked");
    SC_CHECK(!esp_tx_allowed(100000000, true), "100 MHz blocked");

    return sc_test_summary();
}
