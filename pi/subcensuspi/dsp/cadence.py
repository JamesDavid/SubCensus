"""Signal cadence / temporal fingerprint — Python port of shared/core/sc_cadence.c (System §7a).

The Pi is the strongest cadence measurer (it timestamps every reception in `events`), so it
computes the dropout-robust `cadence_*` fields from a device's full event history.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

# Cadence classes match shared/schema (System §7a).
PERIODIC = "periodic"
QUASI_PERIODIC = "quasi-periodic"
EVENT_DRIVEN = "event-driven"
NEAR_CONTINUOUS = "near-continuous"
SEEN_ONCE = "seen-once"

_NEAR_CONT_S = 2.0
_SUPPORT_PERIODIC = 0.75
_FOLD_TOL = 0.18
_PERIODIC_TIGHT = 0.6
_MAX_HARMONIC = 6


@dataclass
class CadenceEstimate:
    cls: str = SEEN_ONCE
    period_s: float = 0.0  # 0 => null (event-driven/seen-once)
    regularity: float = 0.0
    samples: int = 0


def _fold_residual(x: float, p: float) -> float:
    if p <= 0:
        return 0.5
    m = math.floor(x / p + 0.5)
    if m < 1:
        m = 1
    if m > _MAX_HARMONIC:
        m = _MAX_HARMONIC
    r = abs(x - m * p) / p
    return 0.5 if r > 0.5 else r


def _classify_intervals(iv: list[float]) -> CadenceEstimate:
    out = CadenceEstimate()
    if not iv:
        return out
    n = len(iv)
    out.samples = n
    mean = sum(iv) / n
    var = sum(x * x for x in iv) / n - mean * mean
    if var < 0:
        var = 0.0
    cov = math.sqrt(var) / mean if mean > 0 else 1.0
    out.regularity = 1.0 - min(1.0, cov)

    if 0 < mean < _NEAR_CONT_S:
        out.cls = NEAR_CONTINUOUS
        out.period_s = mean
        return out

    mn = min(iv)
    lo, hi = mn * 0.75, mn * 1.25
    in_cluster = [x for x in iv if lo <= x <= hi]
    p = (sum(in_cluster) / len(in_cluster)) if in_cluster else mn

    on_mult = 0
    resid_sum = 0.0
    for x in iv:
        r = _fold_residual(x, p)
        resid_sum += r
        if r <= _FOLD_TOL:
            on_mult += 1
    support = on_mult / n
    mean_resid = resid_sum / n
    folded_tight = 1.0 - min(1.0, mean_resid / 0.25)

    if support >= _SUPPORT_PERIODIC:
        out.cls = PERIODIC if folded_tight >= _PERIODIC_TIGHT else QUASI_PERIODIC
        out.period_s = p
    else:
        out.cls = EVENT_DRIVEN
        out.period_s = 0.0
    return out


def from_timestamps(ts_s: list[int] | list[float]) -> CadenceEstimate:
    """Estimate cadence from reception timestamps (seconds; need not be sorted)."""
    if len(ts_s) <= 1:
        return CadenceEstimate(cls=SEEN_ONCE)
    s = sorted(ts_s)
    iv = [float(s[i] - s[i - 1]) for i in range(1, len(s)) if s[i] - s[i - 1] > 0]
    return _classify_intervals(iv)
