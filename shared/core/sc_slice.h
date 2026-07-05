/* sc_slice.h — RAW timing <-> bit-frame slicing for the edit-before-transmit / field-map
 * editor (Zero §6, Esp M8, System §7b).
 *
 * A generic OOK/level slicer: quantize each signed-duration run to `round(|dur|/unit)` symbol
 * units (>=1) and emit that many bits of the run's level (mark=1, space=0). This turns a raw
 * pulse train into an aligned bit frame the differential analysis (sc_diff) and structured
 * editor (sc_fieldmap) operate on. `sc_slice_encode` is its exact inverse: consecutive same-
 * level bits coalesce into one run of `k*unit` microseconds, so slice(encode(bits)) == bits.
 *
 * This is a crude line-code-agnostic representation (not a protocol decode) — good enough for
 * differential structure discovery and round-trip bit editing of fixed-code / own-device gear
 * (the acknowledged scope, Zero §6). `unit` is typically the shortest dominant symbol duration
 * (sc_feature sym_dur_us[0]). Bits are MSB-first, matching sc_diff / sc_fieldmap.
 */
#ifndef SC_SLICE_H
#define SC_SLICE_H

#include <stddef.h>
#include <stdint.h>

/* Slice `n` signed timings into an MSB-first bit frame in out[cap_bytes]. Returns the number
 * of BITS written (<= cap_bytes*8). `unit_us` must be > 0. */
size_t sc_slice_bits(const int32_t* timings, size_t n, int32_t unit_us, uint8_t* out, size_t cap_bytes);

/* Encode an MSB-first bit frame (`nbits`) back to signed timings in out[cap]. Consecutive
 * same-level bits coalesce into one run of k*unit_us. Returns the number of timings written. */
size_t sc_slice_encode(const uint8_t* frame, size_t nbits, int32_t unit_us, int32_t* out, size_t cap);

#endif /* SC_SLICE_H */
