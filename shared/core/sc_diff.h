/* sc_diff.h — differential bitfield analysis (System §7b tier 1, passive).
 *
 * Across a device's aligned capture corpus, score each bit's change-rate + entropy and
 * segment into static (id/address/preamble), slow-varying (sensor values), and every-frame
 * (sequence/counter). This is the passive, no-TX primitive that seeds the structured-editor
 * overlay (Zero §6) and the host-side field-map proposal (System §8) — which pairs it with
 * checksum discovery (sc_crc.c) and, only with the user's confirmation, writes a field map.
 * NEVER auto-commits (System §7b).
 *
 * Bits are indexed MSB-first: bit i lives in byte i/8 at mask (0x80 >> (i%8)).
 */
#ifndef SC_DIFF_H
#define SC_DIFF_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SC_BIT_STATIC = 0, /* never changes across the corpus */
    SC_BIT_SLOW = 1, /* changes sometimes (candidate sensor value) */
    SC_BIT_COUNTER = 2, /* changes almost every frame (sequence/counter) */
} ScBitClass;

typedef struct {
    float change_rate; /* fraction of adjacent frame-pairs where this bit differs */
    float entropy; /* Shannon entropy of the bit over the corpus (0..1 bit) */
    int distinct; /* 1 if the bit took a single value, 2 if it took both */
    ScBitClass cls;
} ScBitProfile;

/* Analyze `n_frames` frames (each `stride_bytes` apart, `nbits` bits used) into per-bit
 * profiles written to out[0..nbits). Frames are taken in received order for change-rate. */
void sc_diff_analyze(
    const uint8_t* frames,
    size_t n_frames,
    size_t nbits,
    size_t stride_bytes,
    ScBitProfile* out);

#endif /* SC_DIFF_H */
