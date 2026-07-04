"""Parse rtl_433 JSON events into normalized receptions (Pi §4, §5).

rtl_433 with `-F json -M time:iso:tz -M level -M protocol` emits one JSON object per event:
  {"time":"2026-07-04T12:00:00","model":"Acurite-Tower","id":1234,"channel":"A",
   "freq":433.92,"rssi":-60.5,"snr":12.0,"noise":-72.5,"temperature_C":21.3, ...}
A decoded event has "model"; an undecoded/pulse-analyzer trigger has none (routes to
`unknowns`, M4). `freq` is MHz.
"""

from __future__ import annotations

import json

from ..db import Reception


def _to_freq_hz(obj: dict) -> int:
    for key in ("freq", "freq1", "freq2"):
        if key in obj and obj[key] is not None:
            try:
                return int(round(float(obj[key]) * 1_000_000))
            except (TypeError, ValueError):
                pass
    if "freq_hz" in obj:
        try:
            return int(obj["freq_hz"])
        except (TypeError, ValueError):
            pass
    return 0


def _to_float(obj: dict, key: str) -> float | None:
    v = obj.get(key)
    if v is None:
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def parse_event(obj: dict, source: str = "", place: str = "home") -> Reception | None:
    """Return a Reception for a decoded event, or None for an undecoded/non-device line
    (rtl_433 also emits stats/protocol lines that have no 'model')."""
    model = obj.get("model")
    if not model:
        return None
    return Reception(
        ts=str(obj.get("time", "")),
        model=str(model),
        dev_id=str(obj.get("id", obj.get("address", ""))),
        channel=str(obj.get("channel", "")),
        freq_hz=_to_freq_hz(obj),
        rssi=_to_float(obj, "rssi"),
        snr=_to_float(obj, "snr"),
        source=source,
        place=place,
        raw_json=json.dumps(obj, separators=(",", ":"), sort_keys=True),
    )


def parse_line(line: str, source: str = "", place: str = "home") -> Reception | None:
    line = line.strip()
    if not line:
        return None
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None
    if not isinstance(obj, dict):
        return None
    return parse_event(obj, source=source, place=place)
