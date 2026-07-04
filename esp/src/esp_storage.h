/* esp_storage.h — storage-tier selection (Esp §4), pure C so it host-tests.
 *
 * LittleFS is the default (internal flash, capped/rotating captures). If a microSD is detected
 * on VSPI at boot, use the full per-place folder model at "/sd" with no rotation limit beyond
 * card size. Same on-disk schema either way (System §4/§9); only base path + rotation differ.
 */
#ifndef ESP_STORAGE_H
#define ESP_STORAGE_H

#include <stdbool.h>

/* Filesystem base prefix for per-place data: "/sd" when a card is present, else "" (LittleFS). */
const char* esp_storage_base(bool sd_present);

/* Capped/rotating captures apply on LittleFS only; SD is bounded by card size. */
bool esp_storage_rotation_enabled(bool sd_present);

/* Max capture files before rotation: `littlefs_cap` on LittleFS, 0 (unlimited) on SD. */
int esp_storage_max_captures(bool sd_present, int littlefs_cap);

#endif /* ESP_STORAGE_H */
