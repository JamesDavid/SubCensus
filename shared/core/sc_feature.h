/* sc_feature.h — canonical feature vector (System §7).
 *
 * Computes the waveform fields of the shared feature vector from a RAW timing stream.
 * The SAME binning + normalization must run on the Zero (CC1101 RAW) and the Pi
 * (rtl_433/.cu8) so vectors from either land in one k-NN space (System §7, binding).
 *
 * Cadence fields (System §7a) are a PER-DEVICE property derived from reception history,
 * not from a single capture — see sc_cadence.h. They are NOT set here.
 */
#ifndef SC_FEATURE_H
#define SC_FEATURE_H

#include <stddef.h>
#include <stdint.h>

#include "sc_result.h"
#include "sc_types.h"

typedef struct {
    int32_t freq_bin;        /* carrier binned to SC_FREQ_BIN_HZ (Hz) */
    ScModulation modulation; /* provided by caller (capture preset) */
    int32_t sym_dur_us[3];   /* up to 3 dominant symbol durations, ASCENDING; 0 = unused */
    int32_t n_sym_dur;       /* how many of sym_dur_us[] are valid (0..3) */
    int32_t n_symbols;       /* count of non-zero pulses analyzed */
    int32_t est_bitrate;     /* bits/s, ~ 1e6 / shortest dominant symbol */
    int32_t preamble_len;    /* leading run of regular (equal-width) pulses */
    int32_t repeat_count;    /* frames per event (interior long gaps + 1) */
} ScFeatureVector;

/* Compute the waveform feature vector from `n` signed timings captured at `freq_hz`
 * under modulation `mod`. Fills *out. Returns SC_OK, or SC_EMPTY (freq_bin/modulation
 * still set) when there are no usable pulses. */
ScResult sc_feature_compute(
    const int32_t* timings,
    size_t n,
    int32_t freq_hz,
    ScModulation mod,
    ScFeatureVector* out);

#endif /* SC_FEATURE_H */
