#include "sc_slice.h"

static void put_bit(uint8_t* out, size_t cap_bytes, size_t idx, int level) {
    size_t byte = idx / 8;
    if(byte >= cap_bytes) return;
    uint8_t mask = (uint8_t)(0x80u >> (idx % 8));
    if(level)
        out[byte] |= mask;
    else
        out[byte] &= (uint8_t)~mask;
}

size_t sc_slice_bits(
    const int32_t* timings,
    size_t n,
    int32_t unit_us,
    uint8_t* out,
    size_t cap_bytes) {
    if(unit_us <= 0 || cap_bytes == 0) return 0;
    for(size_t i = 0; i < cap_bytes; i++)
        out[i] = 0;
    size_t bit = 0;
    size_t maxbits = cap_bytes * 8;
    for(size_t i = 0; i < n && bit < maxbits; i++) {
        int32_t d = timings[i];
        int level = d > 0 ? 1 : 0;
        int32_t mag = d > 0 ? d : -d;
        if(mag == 0) continue;
        /* round to nearest unit, at least 1 symbol */
        int32_t k = (mag + unit_us / 2) / unit_us;
        if(k < 1) k = 1;
        for(int32_t j = 0; j < k && bit < maxbits; j++)
            put_bit(out, cap_bytes, bit++, level);
    }
    return bit;
}

size_t sc_slice_encode(
    const uint8_t* frame,
    size_t nbits,
    int32_t unit_us,
    int32_t* out,
    size_t cap) {
    if(unit_us <= 0 || cap == 0) return 0;
    size_t nt = 0;
    size_t i = 0;
    while(i < nbits && nt < cap) {
        int level = (frame[i / 8] >> (7 - (i % 8))) & 1;
        size_t run = 0;
        while(i < nbits) {
            int b = (frame[i / 8] >> (7 - (i % 8))) & 1;
            if(b != level) break;
            run++;
            i++;
        }
        int32_t dur = (int32_t)run * unit_us;
        out[nt++] = level ? dur : -dur;
    }
    return nt;
}
