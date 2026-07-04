"""Canonical feature vector — Python port of shared/core/sc_feature.c (System §7).

The SAME binning + normalization as the C core so Zero (RAW) and Pi (IQ/pulse) vectors land
in one k-NN space (System §7, binding). Cadence fields are per-device (see cadence.py), not
set here.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .pulse import cluster

FREQ_BIN_HZ = 5000
_REL_TOL = 0.25
_GAP_FACTOR = 5

# Modulation strings match shared/schema (System §7).
MOD_OOK = "OOK"
MOD_2FSK = "2-FSK"
MOD_TPMS = "TPMS-preset"


def freq_bin(freq_hz: int) -> int:
    """Carrier binned to 5 kHz (round-to-nearest), in Hz — identical to sc_freq_bin."""
    half = FREQ_BIN_HZ // 2
    if freq_hz >= 0:
        return ((freq_hz + half) // FREQ_BIN_HZ) * FREQ_BIN_HZ
    return -(((-freq_hz + half) // FREQ_BIN_HZ) * FREQ_BIN_HZ)


@dataclass
class FeatureVector:
    freq_bin: int = 0
    modulation: str = ""
    sym_dur_us: list[int] = field(default_factory=list)  # ascending, up to 3
    n_symbols: int = 0
    est_bitrate: int = 0
    preamble_len: int = 0
    repeat_count: int = 0


def compute(timings: list[int], freq_hz: int, modulation: str) -> FeatureVector:
    fv = FeatureVector(freq_bin=freq_bin(freq_hz), modulation=modulation)
    if not timings:
        return fv

    nsym = sum(1 for t in timings if abs(t) != 0)
    fv.n_symbols = nsym
    if nsym == 0:
        return fv

    clusters = cluster(timings, _REL_TOL, 3)
    mode_sym = clusters[0].center_us if clusters else 0  # most common (pre ascending sort)
    clusters.sort(key=lambda c: c.center_us)
    fv.sym_dur_us = [c.center_us for c in clusters]

    if clusters and fv.sym_dur_us[0] > 0:
        fv.est_bitrate = int(1000000.0 / fv.sym_dur_us[0] + 0.5)

    # preamble: leading run of consecutive non-zero pulses within tol of the first
    start = 0
    while start < len(timings) and abs(timings[start]) == 0:
        start += 1
    if start < len(timings):
        ref = abs(timings[start])
        tol = int(_REL_TOL * ref)
        if tol < 20:
            tol = 20
        run = 0
        for i in range(start, len(timings)):
            w = abs(timings[i])
            if w == 0:
                continue
            if abs(w - ref) <= tol:
                run += 1
            else:
                break
        fv.preamble_len = run

    # repeat_count: interior long gaps (excluding trailing element) + 1
    gap_threshold = mode_sym * _GAP_FACTOR
    if gap_threshold <= 0:
        gap_threshold = 1
    interior_gaps = 0
    for i in range(len(timings) - 1):
        if timings[i] < 0 and abs(timings[i]) > gap_threshold:
            interior_gaps += 1
    fv.repeat_count = interior_gaps + 1
    return fv
