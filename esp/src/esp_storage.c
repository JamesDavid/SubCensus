#include "esp_storage.h"

const char* esp_storage_base(bool sd_present) {
    return sd_present ? "/sd" : "";
}

bool esp_storage_rotation_enabled(bool sd_present) {
    return !sd_present; /* SD is bounded by card size, not a rotation cap (Esp §4) */
}

int esp_storage_max_captures(bool sd_present, int littlefs_cap) {
    return sd_present ? 0 : littlefs_cap; /* 0 = unlimited */
}
