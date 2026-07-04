#include "esp_capture.h"

static int32_t signed_us(uint16_t ticks, uint8_t level, float ticks_per_us) {
    int32_t us = (int32_t)((float)ticks / ticks_per_us + 0.5f);
    return level ? us : -us;
}

size_t esp_capture_rmt_to_timings(
    const ScRmtItem* items, size_t n, float ticks_per_us, int32_t* out, size_t cap) {
    if(!items || !out || cap == 0 || ticks_per_us <= 0.0f) return 0;
    size_t k = 0;
    for(size_t i = 0; i < n && k < cap; i++) {
        if(items[i].duration0 == 0) break; /* end-of-burst marker */
        out[k++] = signed_us(items[i].duration0, items[i].level0, ticks_per_us);
        if(k >= cap) break;
        if(items[i].duration1 == 0) break; /* trailing half is the terminator */
        out[k++] = signed_us(items[i].duration1, items[i].level1, ticks_per_us);
    }
    return k;
}
