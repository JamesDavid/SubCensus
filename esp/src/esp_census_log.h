/* esp_census_log.h — census_log.csv row building (Esp §4; catalog record System §9 / Zero §5.4).
 *
 * Pure C so it host-tests. The ESP writes the SAME census_log schema as the Zero (generated
 * CENSUS_LOG_HEADER). Metadata rows are kept even after a RAW capture file is rotated out
 * (Esp §4), so `sub_file` may be empty (an RSSI-only "blip" row too, Zero §7).
 */
#ifndef ESP_CENSUS_LOG_H
#define ESP_CENSUS_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char* ts_iso;
    int32_t freq_hz;
    float rssi_dbm;
    int32_t duration_ms;
    const char* preset;
    bool fsk_suspected;
    const char* protocol;     /* "" if undecoded */
    const char* key;          /* "" if undecoded */
    const char* match_name;   /* best candidate (advisory) */
    const char* match_class;
    float match_conf;
    const char* match_source; /* decoder | fingerprint | user | "" */
    const char* sub_file;     /* relative path, or "" for a blip row */
    const char* label;        /* user-confirmed, "" until confirmed */
} EspCensusRow;

/* Build a CSV row (no trailing newline) into out[cap]. Returns bytes written, or -1 on
 * overflow. Column order MUST match the generated CENSUS_LOG_HEADER. */
int esp_census_log_row(const EspCensusRow* r, char* out, size_t cap);

#endif /* ESP_CENSUS_LOG_H */
