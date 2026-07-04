/* esp_place.h — place_id slug + per-place path helpers (Esp §4 storage), pure C so it host-tests.
 *
 * Same Place model as the Zero/Pi (System §4). The on-disk layout is identical whether the tier
 * is LittleFS (base "/") or SD (base "/sd") — only capacity/rotation differ (Esp §4). Paths are
 * built against a caller-supplied base so both tiers reuse this.
 */
#ifndef ESP_PLACE_H
#define ESP_PLACE_H

#include <stddef.h>

/* Slugify a display name into a filesystem-safe place_id (slug + short hash), rename-safe
 * (System §4). Same scheme as the Zero. */
void esp_place_id_from_name(const char* name, char* out_id, size_t cap);

/* base/places/<place_id> */
void esp_place_dir(const char* base, const char* place_id, char* out, size_t cap);

/* base/places/<place_id>/<file> */
void esp_place_file(const char* base, const char* place_id, const char* file, char* out, size_t cap);

/* base/signatures  (GLOBAL brain — never per-place) */
void esp_signatures_dir(const char* base, char* out, size_t cap);

#endif /* ESP_PLACE_H */
