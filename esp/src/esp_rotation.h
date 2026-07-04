/* esp_rotation.h — capped/rotating RAW-capture policy for LittleFS (Esp §4), pure C.
 *
 * Flash is small, so RAW capture files are capped by count and/or total size and the oldest
 * are rotated out; metadata rows in census_log.csv are kept even after a RAW file is deleted
 * (Esp §4). These pure functions decide how many oldest files to evict before adding one.
 */
#ifndef ESP_ROTATION_H
#define ESP_ROTATION_H

#include <stddef.h>

/* Oldest files to delete so that after adding one more, count <= max_count.
 * max_count <= 0 means unlimited (returns 0). */
int esp_rotation_evict_for_count(int current_count, int max_count);

/* Oldest files to delete (from oldest_sizes[0..n), oldest first) so that
 * current_total - evicted + incoming <= max_total. max_total <= 0 => unlimited. */
int esp_rotation_evict_for_size(
    const long* oldest_sizes, int n, long incoming, long current_total, long max_total);

#endif /* ESP_ROTATION_H */
