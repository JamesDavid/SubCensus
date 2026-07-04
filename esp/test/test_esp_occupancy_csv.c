/* test_esp_occupancy_csv.c — occupancy/watchlist CSV IO (Esp §3, System §9). */
#include "esp_occupancy_csv.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_occupancy_csv\n");

    /* occupancy row (shared schema: freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen) */
    ScOccupancyBin b = {433920000, -97.0f, -55.0f, 0.5f, 3, 0};
    char row[128];
    int n = esp_occupancy_row(&b, "2026-07-04T12:00:00", row, sizeof(row));
    SC_CHECK(n > 0, "occupancy row built");
    SC_CHECK_STR(row, "433920000,-97.0,-55.0,0.5000,3,2026-07-04T12:00:00");

    /* watchlist row (freq_hz,modulation,threshold_dbm,occupancy,source) */
    ScWatchlistEntry e = {915000000, SC_MOD_2FSK, -85.0f, 0.8f};
    n = esp_watchlist_row(&e, "recon", row, sizeof(row));
    SC_CHECK(n > 0, "watchlist row built");
    SC_CHECK_STR(row, "915000000,2-FSK,-85.0,0.8000,recon");

    /* unresolved modulation defaults to OOK (Stage B not yet run) */
    ScWatchlistEntry u = {433920000, SC_MOD_UNKNOWN, -80.0f, 0.3f};
    esp_watchlist_row(&u, "recon", row, sizeof(row));
    SC_CHECK_STR(row, "433920000,OOK,-80.0,0.3000,recon");

    /* parse a watchlist row back (Sweep/Camp consume it) */
    ScWatchlistEntry p;
    SC_CHECK(esp_watchlist_parse_line("915000000,2-FSK,-85.0,0.8000,recon", &p), "parsed");
    SC_CHECK_INT(p.freq_hz, 915000000);
    SC_CHECK_INT(p.modulation, SC_MOD_2FSK);
    SC_CHECK_DBL(p.threshold_dbm, -85.0, 0.01);
    SC_CHECK_DBL(p.occupancy, 0.8, 0.001);

    /* user-pin row parses too (source ignored for consumption) */
    SC_CHECK(esp_watchlist_parse_line("315000000,OOK,-90.0,0.1000,user-pin", &p), "pin parsed");
    SC_CHECK_INT(p.freq_hz, 315000000);
    SC_CHECK_INT(p.modulation, SC_MOD_OOK);

    return sc_test_summary();
}
