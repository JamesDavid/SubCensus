"""Differential bitfield analysis — Python port of shared/core/sc_diff.c (System §7b tier 1).

Across a device's aligned capture corpus, score each bit's change-rate + entropy and segment
into static / slow-varying / counter. The Pi is the strongest at the passive layers (its
continuous `events` corpus gives the cleanest differential). Bits are MSB-first.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

STATIC = "static"
SLOW = "slow"
COUNTER = "counter"

_COUNTER_RATE = 0.8


@dataclass
class BitProfile:
    change_rate: float
    entropy: float
    distinct: int  # 1 or 2
    cls: str


def _bit_at(frame: bytes, bit: int) -> int:
    return (frame[bit // 8] >> (7 - (bit % 8))) & 1


def analyze(frames: list[bytes], nbits: int) -> list[BitProfile]:
    """Analyze frames (received order) into per-bit profiles for bits 0..nbits."""
    out: list[BitProfile] = []
    n = len(frames)
    for b in range(nbits):
        if n == 0:
            out.append(BitProfile(0.0, 0.0, 0, STATIC))
            continue
        prev = _bit_at(frames[0], b)
        ones = prev
        changes = 0
        seen0 = prev == 0
        seen1 = prev == 1
        for f in range(1, n):
            v = _bit_at(frames[f], b)
            ones += v
            seen1 = seen1 or v == 1
            seen0 = seen0 or v == 0
            if v != prev:
                changes += 1
            prev = v
        distinct = 2 if (seen0 and seen1) else 1
        change_rate = changes / (n - 1) if n > 1 else 0.0
        p1 = ones / n
        p0 = 1.0 - p1
        entropy = 0.0
        if p1 > 0:
            entropy -= p1 * math.log2(p1)
        if p0 > 0:
            entropy -= p0 * math.log2(p0)
        if distinct == 1:
            cls = STATIC
        elif change_rate >= _COUNTER_RATE:
            cls = COUNTER
        else:
            cls = SLOW
        out.append(BitProfile(change_rate, entropy, distinct, cls))
    return out
