"""Decode plausibility / confidence gate (System §6 "Confidence & honesty").

System §6 is explicit: a classification is a *proposal*, and "every match carries a score +
source" — weak matches must be flagged, not presented as fact. The raw collector path trusts
whatever rtl_433 emitted verbatim, so a physically-impossible decode (an Opus reporting
``temp -40 °C`` — which is that decoder's raw-zero null value — or an Efergy claiming 96 A on a
count-of-one) looks exactly like a real device. This module restores the §6 honesty step:
score each decode on (a) whether its decoded values are physically plausible and (b) how well
repeated sightings corroborate it, and surface that confidence + the reasons.

Presentation/analysis only — RX-only, no re-decoding of RF here (we score the values rtl_433
already produced). It does NOT delete anything or auto-relabel (§6): it proposes "this looks
like noise, and here's why," and the user decides.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field

from .readings import _PLUMBING

# Physically sane ranges for common rtl_433 measurement fields. (low, high, unit, label). A value
# outside the range is a hard red flag; the extra `sentinel` value (if set) is the decoder's
# raw-zero / null reading and is treated as "no real reading".
_RANGES: dict[str, dict] = {
    "temperature_C": {"low": -30.0, "high": 60.0, "unit": "°C", "sentinel": -40.0},
    "temperature_F": {"low": -22.0, "high": 140.0, "unit": "°F"},
    "humidity": {"low": 0.0, "high": 100.0, "unit": "%"},
    "moisture": {"low": 0.0, "high": 100.0, "unit": "%"},
    "pressure_kPa": {"low": 80.0, "high": 1200.0, "unit": " kPa"},
    "current_A": {"low": 0.0, "high": 90.0, "unit": " A"},
    "current": {"low": 0.0, "high": 90.0, "unit": " A"},
    "power_W": {"low": 0.0, "high": 30000.0, "unit": " W"},
    "wind_avg_km_h": {"low": 0.0, "high": 200.0, "unit": " km/h"},
    "rain_mm": {"low": 0.0, "high": 2000.0, "unit": " mm"},
}

# rtl_433 decoders with a weak/absent checksum that are well-known to false-match on noise. A
# match from one of these starts with a lower prior (still shown if repeated + plausible).
_NOISY_DECODERS = {"efergy-e2ct", "efergy-optical"}

# A device must clear this to be presented as "real" by default (System §6 honesty gate).
CONFIDENCE_FLOOR = 0.5


@dataclass
class Assessment:
    confidence: float                      # 0..1, honest score (§6)
    plausible: bool                        # confidence >= floor AND no hard red flag
    reasons: list[str] = field(default_factory=list)  # why (esp. why-not) — human readable

    def as_dict(self) -> dict:
        return {"confidence": round(self.confidence, 2), "plausible": self.plausible,
                "reasons": self.reasons}


def _num(v) -> float | None:
    if isinstance(v, bool):
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def _fmt(v: float) -> str:
    return str(int(v)) if v == int(v) else f"{round(v, 2)}"


def assess(raw_json: str | None, model: str = "", count: int = 1) -> Assessment:
    """Score one decode's honesty (System §6). `count` is how many times the device has been
    heard (corroboration). Returns confidence 0..1, a plausible flag, and human-readable reasons."""
    reasons: list[str] = []
    hard_flag = False

    data: dict = {}
    if raw_json:
        try:
            obj = json.loads(raw_json)
            if isinstance(obj, dict):
                data = obj
        except (ValueError, TypeError):
            pass

    # measurement fields only (identity/RF/timing are plumbing, not evidence of a real reading)
    fields = {k: v for k, v in data.items() if k not in _PLUMBING and v is not None}
    numeric = {k: _num(v) for k, v in fields.items()}
    numeric = {k: v for k, v in numeric.items() if v is not None}

    # 1. all-zero / sentinel payload -> the decoder locked onto noise and returned its null frame
    #    (this is the classic "-40 °C, 0 %" Opus signature).
    sentinel_hit = False
    for k, v in numeric.items():
        rng = _RANGES.get(k)
        if rng and "sentinel" in rng and v == rng["sentinel"]:
            sentinel_hit = True
            reasons.append(f"{k.replace('_', ' ')} {_fmt(v)}{rng['unit']} is the sensor's null "
                           f"value (raw 0) — not a real reading")
    if numeric and all(v == 0 for v in numeric.values()):
        hard_flag = True
        reasons.append("all sensor values are zero — an empty/null frame (likely a false decode)")
    if sentinel_hit:
        hard_flag = True

    # 2. out-of-range values -> physically impossible for the claimed device
    for k, v in numeric.items():
        rng = _RANGES.get(k)
        if not rng:
            continue
        if v < rng["low"] or v > rng["high"]:
            hard_flag = True
            reasons.append(f"{k.replace('_', ' ')} {_fmt(v)}{rng['unit']} is outside the "
                           f"plausible range {_fmt(rng['low'])}–{_fmt(rng['high'])}{rng['unit']}")

    # 3. corroboration: a periodic sensor heard once over a long run is almost always noise
    #    (each false decode gets a fresh random id). Repetition is the strongest honest signal.
    if count <= 1:
        reasons.append("heard only once — no repetition to corroborate it")

    # --- score ---
    if hard_flag:
        confidence = 0.1
    else:
        confidence = 0.55                       # base for a clean decoder match
        if model.lower() in _NOISY_DECODERS:
            confidence -= 0.15
            reasons.append(f"{model} is a weak/checksum-light decoder that false-matches on noise")
        if count >= 10:
            confidence += 0.35
        elif count >= 3:
            confidence += 0.2
        elif count == 2:
            confidence += 0.1
        else:  # count <= 1
            confidence -= 0.25
        confidence = max(0.0, min(1.0, confidence))

    plausible = (not hard_flag) and confidence >= CONFIDENCE_FLOOR
    return Assessment(confidence=confidence, plausible=plausible, reasons=reasons)
