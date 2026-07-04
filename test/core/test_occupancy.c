/* test_occupancy.c — occupancy accumulation + watchlist derivation (System §9). */
#include "sc_occupancy.h"
#include "sc_test.h"

int main(void) {
    printf("test_occupancy\n");

    /* Online accumulator: 10 samples, 4 above threshold -80, two separate bursts. */
    ScOccupancyAccum a;
    sc_occupancy_accum_init(&a, 433920000);
    double rssi[] = {-95, -70, -68, -96, -97, -60, -94, -93, -65, -95};
    /* above -80: idx1(-70),2(-68),5(-60),8(-65) -> 4 above; bursts at {1,2},{5},{8} = 3 crossings */
    for(int i = 0; i < 10; i++) sc_occupancy_accum_sample(&a, rssi[i], -80.0, 1000 + i);
    ScOccupancyBin bin;
    sc_occupancy_accum_finish(&a, &bin);
    SC_CHECK_INT(bin.freq_hz, 433920000);
    SC_CHECK_DBL(bin.occupancy, 0.4, 0.001);
    SC_CHECK_INT(bin.crossings, 3);
    SC_CHECK_DBL(bin.peak_rssi, -60, 0.001);
    SC_CHECK_DBL(bin.noise_floor, -97, 0.001);
    SC_CHECK_INT((int)bin.last_seen, 1008); /* last above-sample was idx 8 */

    /* Merge a second pass (accumulate): peak=max, crossings summed, occupancy averaged. */
    ScOccupancyBin pass2 = {433920000, -98.0, -55.0, 0.6, 2, 2000};
    sc_occupancy_merge(&bin, 10, &pass2, 10);
    SC_CHECK_DBL(bin.peak_rssi, -55, 0.001);
    SC_CHECK_INT(bin.crossings, 5);
    SC_CHECK_DBL(bin.occupancy, 0.5, 0.001); /* (0.4*10 + 0.6*10)/20 */
    SC_CHECK_INT((int)bin.last_seen, 2000);

    /* Watchlist derivation: three bins, cutoff 0.3, sorted by occupancy desc. */
    ScOccupancyBin bins[3] = {
        {315000000, -100, -70, 0.20, 3, 5000}, /* below cutoff -> excluded */
        {433920000, -98, -55, 0.50, 5, 6000},
        {915000000, -99, -60, 0.80, 9, 7000},
    };
    ScWatchlistEntry wl[3];
    size_t nw = sc_watchlist_from_occupancy(bins, 3, 0.30, 12.0, wl, 3);
    SC_CHECK_INT(nw, 2);
    SC_CHECK_INT(wl[0].freq_hz, 915000000); /* busiest first (Auto pick) */
    SC_CHECK_INT(wl[1].freq_hz, 433920000);
    SC_CHECK_DBL(wl[0].threshold_dbm, -87.0, 0.001); /* -99 + 12 */
    SC_CHECK_INT(wl[0].modulation, SC_MOD_UNKNOWN);  /* Stage B resolves later */

    return sc_test_summary();
}
