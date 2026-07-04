/* sc_sub.h — Flipper `.sub` RAW file parse/encode (Zero §5.1, §5.4).
 *
 * Standard Flipper SubGhz RAW format so existing tools/replay work:
 *   Filetype: Flipper SubGhz RAW File
 *   Version: 1
 *   Frequency: 433920000
 *   Preset: FuriHalSubGhzPresetOok650Async
 *   Protocol: RAW
 *   RAW_Data: 291 -100 300 -8850 ...
 * RAW_Data values are signed durations in microseconds: positive = level HIGH (mark),
 * negative = level LOW (space). Multiple RAW_Data lines concatenate in order.
 *
 * Host-compilable; no allocation (caller provides the timings buffer) so the same code
 * runs on the Flipper and in host tests.
 */
#ifndef SC_SUB_H
#define SC_SUB_H

#include <stddef.h>
#include <stdint.h>

#include "sc_result.h"

typedef struct {
    int32_t frequency;   /* Hz; 0 if absent */
    char preset[64];     /* SDK preset name; "" if absent */
    char protocol[32];   /* usually "RAW"; "" if absent */
} ScSubMeta;

/* Parse `.sub` text (len bytes). Fills *meta and writes up to `cap` signed timings into
 * out_timings, setting *out_n to the number parsed.
 * Returns SC_OK, SC_TRUNCATED if more than `cap` timings were present (out_n == cap),
 * or SC_ERR on malformed input. meta/out_n may be NULL if not wanted. */
ScResult sc_sub_parse(
    const char* text,
    size_t len,
    ScSubMeta* meta,
    int32_t* out_timings,
    size_t cap,
    size_t* out_n);

/* Encode meta + `n` signed timings to `.sub` text in out[cap] (NUL-terminated).
 * `values_per_line` RAW_Data values per line (0 => default 512). Sets *out_len to bytes
 * written (excluding NUL). Returns SC_OK or SC_TRUNCATED if the buffer was too small. */
ScResult sc_sub_encode(
    const ScSubMeta* meta,
    const int32_t* timings,
    size_t n,
    char* out,
    size_t cap,
    size_t values_per_line,
    size_t* out_len);

#endif /* SC_SUB_H */
