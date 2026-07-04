/* esp_capture.h — RMT edge-capture decode (Esp §3), pure C so it host-tests.
 *
 * The RMT peripheral timestamps GDO0 transitions in hardware; each RMT symbol holds two
 * (level, duration) pairs. This converts an RMT symbol buffer into the SAME signed-microsecond
 * timing array the shared feature vector / .sub encoder consume (positive = mark/high,
 * negative = space/low). The firmware maps real rmt_symbol_word_t to ScRmtItem; this decode is
 * the ESP analog of the Flipper's async RAW capture and is unit-tested off-device.
 */
#ifndef ESP_CAPTURE_H
#define ESP_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

/* Portable mirror of one RMT symbol (two level/duration pairs). duration is in RMT ticks. */
typedef struct {
    uint16_t duration0;
    uint8_t level0;
    uint16_t duration1;
    uint8_t level1;
} ScRmtItem;

/* Convert `n` RMT symbols to signed us timings in out[cap]. `ticks_per_us` scales RMT ticks
 * to microseconds (e.g. APB 80 MHz / clk_div 80 => 1 tick = 1 us => ticks_per_us=1).
 * A zero-duration entry marks the end of a burst and stops decode. Returns the number of
 * timings written (<= cap). */
size_t esp_capture_rmt_to_timings(
    const ScRmtItem* items, size_t n, float ticks_per_us, int32_t* out, size_t cap);

#endif /* ESP_CAPTURE_H */
