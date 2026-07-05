/* esp_occupancy_csv.h — occupancy.csv / watchlist.csv row IO (Esp §3, System §9), pure C.
 *
 * The occupancy accumulation + watchlist derivation live in shared/core (sc_occupancy); this
 * is just the shared-schema CSV serialization the ESP writes/reads. Recon emits these; Sweep/
 * Camp consume watchlist.csv. Same artifacts as the Zero/Pi (§9a) — tool-agnostic.
 */
#ifndef ESP_OCCUPANCY_CSV_H
#define ESP_OCCUPANCY_CSV_H

#include <stdbool.h>
#include <stddef.h>

#include "sc_occupancy.h"

/* occupancy.csv row: freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen.
 * last_seen is an ISO string the caller formats from the bin's epoch. */
int esp_occupancy_row(const ScOccupancyBin* b, const char* last_seen_iso, char* out, size_t cap);

/* watchlist.csv row: freq_hz,modulation,threshold_dbm,occupancy,source. */
int esp_watchlist_row(const ScWatchlistEntry* e, const char* source, char* out, size_t cap);

/* Parse a watchlist.csv DATA row (not the header) into *out (freq/modulation/threshold/
 * occupancy). Returns true on success. Used by Sweep/Camp to consume the watchlist. */
bool esp_watchlist_parse_line(const char* line, ScWatchlistEntry* out);

/* Read the `source` column (field 5: recon | user-pin | user-exclude) of a watchlist.csv row into
 * dst[cap]. Returns true on success. Lets Recon preserve user pins/exclusions across re-runs and
 * Reset (System §9). */
bool esp_watchlist_parse_source(const char* line, char* dst, size_t cap);

/* Parse an occupancy.csv DATA row into *out (freq/noise_floor/peak_rssi/occupancy/crossings;
 * last_seen is left 0 — the ISO wall-clock isn't needed for the accumulate merge). Returns true
 * on success. Used to read back the prior pass for cumulative Recon (System §9 accumulate). */
bool esp_occupancy_parse_line(const char* line, ScOccupancyBin* out);

#endif /* ESP_OCCUPANCY_CSV_H */
