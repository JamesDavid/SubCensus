"""Gated k-NN classification — Python port of shared/core/sc_knn.c (System §6).

Same fixed scales/weights + cadence soft-adjust as the C core, so Zero- and Pi-derived
vectors score identically in one k-NN space. Advisory only — never auto-relabels (System §6).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

from .feature import FeatureVector

_SCALE = {"sym": 300.0, "nsym": 50.0, "bitrate": 2000.0, "preamble": 8.0, "repeat": 3.0}
_W = {"sym": 1.0, "nsym": 0.3, "bitrate": 0.5, "preamble": 0.5, "repeat": 0.3}
_MISSING_PENALTY = 1.0

_CADENCE_AGREE = 1.15
_CADENCE_PERIOD_AGREE = 1.10
_CADENCE_DISAGREE = 0.80
_PERIOD_TOL = 0.20


@dataclass
class Fingerprint:
    fv: FeatureVector
    cadence_class: str = ""  # "" => unknown
    period_s: float = 0.0
    device_name: str = ""
    device_class: str = ""


@dataclass
class KnnQuery:
    fv: FeatureVector
    cadence_class: str = ""
    period_s: float = 0.0


@dataclass
class KnnMatch:
    index: int
    distance: float
    confidence: float
    fingerprint: Fingerprint = field(default=None)


def _term(a: float, b: float, scale: float, w: float) -> float:
    return w * ((a - b) / scale) ** 2


def _gated_distance(a: FeatureVector, b: FeatureVector) -> float:
    d2 = 0.0
    for i in range(3):
        ha = i < len(a.sym_dur_us)
        hb = i < len(b.sym_dur_us)
        if ha and hb:
            d2 += _term(a.sym_dur_us[i], b.sym_dur_us[i], _SCALE["sym"], _W["sym"])
        elif ha != hb:
            d2 += _W["sym"] * _MISSING_PENALTY
    d2 += _term(a.n_symbols, b.n_symbols, _SCALE["nsym"], _W["nsym"])
    d2 += _term(a.est_bitrate, b.est_bitrate, _SCALE["bitrate"], _W["bitrate"])
    d2 += _term(a.preamble_len, b.preamble_len, _SCALE["preamble"], _W["preamble"])
    d2 += _term(a.repeat_count, b.repeat_count, _SCALE["repeat"], _W["repeat"])
    return math.sqrt(d2)


def _cadence_adjust(conf: float, qc: str, qp: float, cc: str, cp: float) -> float:
    if not qc or not cc:  # soft: no cadence data => no change (System §6)
        return conf
    if qc == cc:
        conf *= _CADENCE_AGREE
        if qp > 0 and cp > 0:
            rel = abs(qp - cp) / max(qp, cp)
            if rel < _PERIOD_TOL:
                conf *= _CADENCE_PERIOD_AGREE
    else:
        conf *= _CADENCE_DISAGREE
    return max(0.0, min(1.0, conf))


def match(q: KnnQuery, cands: list[Fingerprint], topn: int = 3) -> list[KnnMatch]:
    """Gate on freq_bin+modulation, rank survivors by distance, cadence-adjust confidence."""
    results: list[KnnMatch] = []
    for i, c in enumerate(cands):
        if c.fv.freq_bin != q.fv.freq_bin:
            continue
        if c.fv.modulation != q.fv.modulation:
            continue
        d = _gated_distance(q.fv, c.fv)
        conf = 1.0 / (1.0 + d)
        conf = _cadence_adjust(conf, q.cadence_class, q.period_s, c.cadence_class, c.period_s)
        results.append(KnnMatch(index=i, distance=d, confidence=conf, fingerprint=c))
    results.sort(key=lambda m: m.confidence, reverse=True)
    return results[:topn]
