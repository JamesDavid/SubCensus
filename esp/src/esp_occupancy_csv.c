#include "esp_occupancy_csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_types.h"

int esp_occupancy_row(const ScOccupancyBin* b, const char* last_seen_iso, char* out, size_t cap) {
    int n = snprintf(out, cap, "%ld,%.1f,%.1f,%.4f,%ld,%s", (long)b->freq_hz,
                     (double)b->noise_floor, (double)b->peak_rssi, (double)b->occupancy,
                     (long)b->crossings, last_seen_iso ? last_seen_iso : "");
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

int esp_watchlist_row(const ScWatchlistEntry* e, const char* source, char* out, size_t cap) {
    const char* mod = sc_modulation_str(e->modulation);
    if(mod[0] == '\0') mod = "OOK"; /* Stage B resolves; default OOK if unset */
    int n = snprintf(out, cap, "%ld,%s,%.1f,%.4f,%s", (long)e->freq_hz, mod,
                     (double)e->threshold_dbm, (double)e->occupancy, source ? source : "recon");
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

/* Copy field #idx (0-based, comma-separated, up to newline) of `line` into dst[cap]. */
static bool field(const char* line, int idx, char* dst, size_t cap) {
    const char* p = line;
    for(int i = 0; i < idx; i++) {
        while(*p && *p != ',' && *p != '\n') p++;
        if(*p != ',') return false;
        p++;
    }
    const char* e = p;
    while(*e && *e != ',' && *e != '\n' && *e != '\r') e++;
    size_t n = (size_t)(e - p);
    if(n >= cap) n = cap - 1;
    memcpy(dst, p, n);
    dst[n] = '\0';
    return true;
}

bool esp_watchlist_parse_line(const char* line, ScWatchlistEntry* out) {
    if(!line || !out) return false;
    /* freq_hz,modulation,threshold_dbm,occupancy,source */
    char freq[24], mod[16], thr[24], occ[24];
    if(!field(line, 0, freq, sizeof(freq)) || !field(line, 1, mod, sizeof(mod)) ||
       !field(line, 2, thr, sizeof(thr)) || !field(line, 3, occ, sizeof(occ)))
        return false;
    out->freq_hz = (int32_t)strtol(freq, NULL, 10);
    out->modulation = sc_modulation_from_str(mod);
    out->threshold_dbm = (float)atof(thr);
    out->occupancy = (float)atof(occ);
    return true;
}
