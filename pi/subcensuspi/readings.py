"""Human-readable decoded readings (Pi §5, §7).

Every reception's full rtl_433 JSON is kept verbatim in `events.raw_json`. That JSON carries the
*decoded payload* — the numbers a person actually cares about: temperature, humidity, power,
tyre pressure, battery state, … This turns one such JSON blob into a short, readable line for the
dashboard (Devices "Latest reading" column + the Live feed), hiding the plumbing keys (identity,
RF, timing) that are already shown in their own columns.

Presentation only — no decoding happens here (rtl_433 already decoded it); we just format.
"""

from __future__ import annotations

import json

# Keys that are identity / RF / timing plumbing, not a measurement — shown elsewhere (Model, ID,
# Band, SNR, Last seen) or irrelevant to a human reading. Everything NOT in here is a "reading".
_PLUMBING = {
    "time", "model", "id", "channel", "freq", "freq1", "freq2",
    "rssi", "snr", "noise", "mod", "mic", "type", "subtype",
    "len", "length", "protocol", "sequence_num", "seq", "device",
}

# Known measurement keys -> (label, unit suffix). Anything not listed falls back to a generic
# snake_case -> words label with the raw value (so new rtl_433 fields still show, just less pretty).
_KNOWN: dict[str, tuple[str, str]] = {
    "temperature_C": ("temp", "°C"),
    "temperature_F": ("temp", "°F"),
    "temperature_1_C": ("temp1", "°C"),
    "temperature_2_C": ("temp2", "°C"),
    "setpoint_C": ("setpoint", "°C"),
    "humidity": ("humidity", "%"),
    "moisture": ("moisture", "%"),
    "pressure_kPa": ("pressure", " kPa"),
    "pressure_hPa": ("pressure", " hPa"),
    "pressure_PSI": ("pressure", " PSI"),
    "wind_avg_km_h": ("wind", " km/h"),
    "wind_max_km_h": ("gust", " km/h"),
    "wind_dir_deg": ("wind dir", "°"),
    "rain_mm": ("rain", " mm"),
    "power_W": ("power", " W"),
    "power_kW": ("power", " kW"),
    "energy_kWh": ("energy", " kWh"),
    "current_A": ("current", " A"),
    "current": ("current", " A"),
    "voltage_V": ("voltage", " V"),
    "voltage": ("voltage", " V"),
    "depth_cm": ("depth", " cm"),
    "light_lux": ("light", " lux"),
    "uv": ("UV", ""),
}


def _fmt_num(v: float) -> str:
    """Trim trailing .0 so 21.0 -> 21 but 21.3 stays 21.3."""
    if isinstance(v, bool):  # bool is an int subclass — never format True/False as a number
        return str(v)
    f = float(v)
    return str(int(f)) if f == int(f) else f"{round(f, 2)}"


def reading_fields(raw_json: str | None) -> list[tuple[str, str]]:
    """Parse raw rtl_433 JSON into an ordered list of (label, formatted_value) measurement pairs.
    Empty list on missing/garbage JSON or a plumbing-only record."""
    if not raw_json:
        return []
    try:
        data = json.loads(raw_json)
    except (ValueError, TypeError):
        return []
    if not isinstance(data, dict):
        return []
    out: list[tuple[str, str]] = []
    for key, val in data.items():
        if key in _PLUMBING or val is None:
            continue
        # battery_ok is a 0/1 (or bool) health flag — read it as words, not "battery ok 1".
        if key in ("battery_ok", "battery"):
            ok = bool(val) if key == "battery_ok" else str(val).lower() not in ("0", "low", "false")
            out.append(("battery", "OK" if ok else "LOW"))
            continue
        if key in _KNOWN:
            label, suffix = _KNOWN[key]
            num = _fmt_num(val) if isinstance(val, (int, float)) else str(val)
            out.append((label, f"{num}{suffix}"))
        else:  # unknown field: still show it (snake_case -> words), generic value
            label = key.replace("_", " ").strip()
            num = _fmt_num(val) if isinstance(val, (int, float)) else str(val)
            out.append((label, num))
    return out


# rtl_433 raw-payload fields, in preference order. `-M bits` / raw decoders expose the demodulated
# bits under one of these; keeping them means a wrong fingerprint stays recoverable (System §7b).
_RAW_BIT_FIELDS = ("data", "code", "codes", "rows", "bits", "mic_data")


def raw_bits(raw_json: str | None) -> str:
    """The raw demodulated payload of a reception (hex/bit string), independent of how it was
    decoded — the evidence you'd re-interpret if the fingerprint guess was wrong. Empty if rtl_433
    didn't emit any raw field (enable ``-M bits`` / all_protocols to keep them)."""
    if not raw_json:
        return ""
    try:
        obj = json.loads(raw_json)
    except (ValueError, TypeError):
        return ""
    if not isinstance(obj, dict):
        return ""
    for k in _RAW_BIT_FIELDS:
        v = obj.get(k)
        if v:
            return " ".join(str(x) for x in v) if isinstance(v, list) else str(v)
    return ""


def humanize_reading(raw_json: str | None) -> str:
    """One-line human-readable summary of a reception's decoded payload, e.g.
    ``temp 21.3°C · humidity 45%`` or ``power 812 W · battery OK``. Empty string when there is no
    measurement content (identity-only frame, unparseable, etc.)."""
    return " · ".join(f"{label} {value}" for label, value in reading_fields(raw_json))
