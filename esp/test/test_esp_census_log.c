/* test_esp_census_log.c — census_log row building (Esp §4, Zero §5.4). */
#include <string.h>

#include "esp_census_log.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_census_log\n");

    EspCensusRow r = {
        .ts_iso = "2026-07-04T12:00:00",
        .freq_hz = 433920000,
        .rssi_dbm = -60.5f,
        .duration_ms = 1500,
        .preset = "OOK650",
        .fsk_suspected = false,
        .protocol = "Princeton",
        .key = "0x1A2B3C",
        .match_name = "PT2262 remote",
        .match_class = "remote",
        .match_conf = 0.82f,
        .match_source = "decoder",
        .sub_file = "captures/20260704_120000_433920_OOK650.sub",
        .label = "",
    };
    char buf[256];
    int n = esp_census_log_row(&r, buf, sizeof(buf));
    SC_CHECK(n > 0, "row built");
    SC_CHECK_STR(
        buf,
        "2026-07-04T12:00:00,433920000,-60.5,1500,OOK650,0,Princeton,0x1A2B3C,"
        "PT2262 remote,remote,0.82,decoder,"
        "captures/20260704_120000_433920_OOK650.sub,");

    /* undecoded blip row: empty protocol/match/sub_file, fsk_suspected set */
    EspCensusRow blip = {
        .ts_iso = "2026-07-04T12:01:00", .freq_hz = 315000000, .rssi_dbm = -70.0f,
        .duration_ms = 0, .preset = "OOK650", .fsk_suspected = true,
    };
    n = esp_census_log_row(&blip, buf, sizeof(buf));
    SC_CHECK(n > 0, "blip row built");
    SC_CHECK_STR(buf, "2026-07-04T12:01:00,315000000,-70.0,0,OOK650,1,,,,,0.00,,,");

    /* overflow -> -1 */
    char tiny[8];
    SC_CHECK_INT(esp_census_log_row(&r, tiny, sizeof(tiny)), -1);

    return sc_test_summary();
}
