#include "esp_census_log.h"

#include <stdio.h>

static const char* nz(const char* s) {
    return s ? s : "";
}

int esp_census_log_row(const EspCensusRow* r, char* out, size_t cap) {
    /* order matches CENSUS_LOG_HEADER:
     * ts_iso,freq_hz,rssi_dbm,duration_ms,preset,fsk_suspected,protocol,key,
     * match_name,match_class,match_conf,match_source,sub_file,label */
    int n = snprintf(
        out, cap, "%s,%ld,%.1f,%ld,%s,%d,%s,%s,%s,%s,%.2f,%s,%s,%s",
        nz(r->ts_iso), (long)r->freq_hz, (double)r->rssi_dbm, (long)r->duration_ms,
        nz(r->preset), r->fsk_suspected ? 1 : 0, nz(r->protocol), nz(r->key),
        nz(r->match_name), nz(r->match_class), (double)r->match_conf, nz(r->match_source),
        nz(r->sub_file), nz(r->label));
    if(n < 0 || (size_t)n >= cap) return -1;
    return n;
}
