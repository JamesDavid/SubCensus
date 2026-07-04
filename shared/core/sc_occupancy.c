#include "sc_occupancy.h"

#include <string.h>

void sc_occupancy_accum_init(ScOccupancyAccum* a, int32_t freq_hz) {
    memset(a, 0, sizeof(*a));
    a->freq_hz = freq_hz;
}

void sc_occupancy_accum_sample(ScOccupancyAccum* a, double rssi_dbm, double threshold_dbm, int64_t ts_s) {
    if(!a->started) {
        a->peak_rssi = rssi_dbm;
        a->noise_floor = rssi_dbm;
        a->started = true;
    } else {
        if(rssi_dbm > a->peak_rssi) a->peak_rssi = rssi_dbm;
        if(rssi_dbm < a->noise_floor) a->noise_floor = rssi_dbm;
    }
    a->total++;
    bool above = rssi_dbm >= threshold_dbm;
    if(above) {
        a->above++;
        a->last_seen = ts_s;
        if(!a->prev_above) a->crossings++;
    }
    a->prev_above = above;
}

void sc_occupancy_accum_finish(const ScOccupancyAccum* a, ScOccupancyBin* out) {
    memset(out, 0, sizeof(*out));
    out->freq_hz = a->freq_hz;
    out->peak_rssi = a->peak_rssi;
    out->noise_floor = a->noise_floor;
    out->crossings = a->crossings;
    out->last_seen = a->last_seen;
    out->occupancy = a->total > 0 ? (double)a->above / (double)a->total : 0.0;
}

void sc_occupancy_merge(ScOccupancyBin* acc, int32_t acc_weight, const ScOccupancyBin* in, int32_t in_weight) {
    if(acc_weight < 0) acc_weight = 0;
    if(in_weight < 0) in_weight = 0;
    int64_t total = (int64_t)acc_weight + in_weight;

    if(in->peak_rssi > acc->peak_rssi) acc->peak_rssi = in->peak_rssi;
    acc->crossings += in->crossings;
    if(in->last_seen > acc->last_seen) acc->last_seen = in->last_seen;
    if(total > 0) {
        acc->occupancy =
            (acc->occupancy * acc_weight + in->occupancy * in_weight) / (double)total;
        acc->noise_floor =
            (acc->noise_floor * acc_weight + in->noise_floor * in_weight) / (double)total;
    }
    if(acc->freq_hz == 0) acc->freq_hz = in->freq_hz;
}

size_t sc_watchlist_from_occupancy(
    const ScOccupancyBin* bins,
    size_t n,
    double occ_cutoff,
    double margin_db,
    ScWatchlistEntry* out,
    size_t cap) {
    if(!bins || !out || cap == 0) return 0;
    size_t count = 0;
    for(size_t i = 0; i < n; i++) {
        if(bins[i].occupancy < occ_cutoff) continue;
        ScWatchlistEntry e;
        e.freq_hz = bins[i].freq_hz;
        e.modulation = SC_MOD_UNKNOWN; /* Stage B resolves */
        e.threshold_dbm = bins[i].noise_floor + margin_db;
        e.occupancy = bins[i].occupancy;

        /* insertion sort by occupancy desc into top-`cap` */
        size_t limit = count < cap ? count : cap;
        size_t pos = limit;
        while(pos > 0 && out[pos - 1].occupancy < e.occupancy) pos--;
        if(pos < cap) {
            size_t last = (count < cap) ? count : cap - 1;
            for(size_t j = last; j > pos; j--) out[j] = out[j - 1];
            out[pos] = e;
            if(count < cap) count++;
        }
    }
    return count;
}
