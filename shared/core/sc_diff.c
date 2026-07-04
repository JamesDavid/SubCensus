#include "sc_diff.h"

#include <math.h>
#include <string.h>

#define SC_DIFF_COUNTER_RATE 0.8 /* change-rate above this => counter/every-frame */

static int bit_at(const uint8_t* frames, size_t stride, size_t frame, size_t bit) {
    const uint8_t* f = frames + frame * stride;
    return (f[bit / 8] >> (7 - (bit % 8))) & 1;
}

void sc_diff_analyze(
    const uint8_t* frames,
    size_t n_frames,
    size_t nbits,
    size_t stride_bytes,
    ScBitProfile* out) {
    for(size_t b = 0; b < nbits; b++) {
        ScBitProfile p;
        memset(&p, 0, sizeof(p));

        if(n_frames == 0) {
            p.cls = SC_BIT_STATIC;
            p.distinct = 0;
            out[b] = p;
            continue;
        }

        int ones = 0;
        int changes = 0;
        int prev = bit_at(frames, stride_bytes, 0, b);
        int seen0 = (prev == 0), seen1 = (prev == 1);
        ones += prev;
        for(size_t f = 1; f < n_frames; f++) {
            int v = bit_at(frames, stride_bytes, f, b);
            ones += v;
            if(v)
                seen1 = 1;
            else
                seen0 = 1;
            if(v != prev) changes++;
            prev = v;
        }

        p.distinct = (seen0 && seen1) ? 2 : 1;
        p.change_rate = (n_frames > 1) ? (float)changes / (float)(n_frames - 1) : 0.0;

        float p1 = (float)ones / (float)n_frames;
        float p0 = 1.0 - p1;
        float h = 0.0;
        if(p1 > 0.0) h -= p1 * log2f(p1);
        if(p0 > 0.0) h -= p0 * log2f(p0);
        p.entropy = h;

        if(p.distinct == 1) {
            p.cls = SC_BIT_STATIC;
        } else if(p.change_rate >= SC_DIFF_COUNTER_RATE) {
            p.cls = SC_BIT_COUNTER;
        } else {
            p.cls = SC_BIT_SLOW;
        }
        out[b] = p;
    }
}
