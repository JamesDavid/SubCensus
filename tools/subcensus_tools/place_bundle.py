"""Build the shared analysis bundle from a CSV-based place folder (System §8).

A SubCensusZero place folder (and an Esp SD place) stores census_log.csv / occupancy.csv /
watchlist.csv (System §4/§9). This rolls one into the SAME structured bundle the Pi produces
from SQLite — so a Zero place and a Pi place are interchangeable inputs to export_place /
analyze_place (System §8). Reuses shared/schema + taxonomy; no hardware.

Cadence here is coarse (the Zero is a weak walk-around measurer, §5.5); the stationary Pi
computes the canonical dropout-robust cadence.
"""

from __future__ import annotations

import csv
import datetime as _dt
import json
from pathlib import Path

# ISM band -> typical devices + typical cadences (reference grounding, System §8).
ISM_BANDS = {
    "315 MHz": ["car-fob", "tpms", "pir-motion", "remote"],
    "433.92 MHz": ["remote", "weather", "tpms", "doorbell", "garage", "smart-home"],
    "868 MHz": ["weather", "energy-meter", "water/gas-meter"],
    "915 MHz": ["energy-meter", "water/gas-meter", "weather", "beacon"],
}
TYPICAL_CADENCE = {
    "weather": "periodic ~30-60 s", "energy-meter": "periodic ~5 min / hourly",
    "tpms": "motion-triggered (event-driven)", "remote": "event-driven / one-shot",
    "pir-motion": "event-driven", "doorbell": "event-driven / one-shot",
}

FREQ_BIN_HZ = 5000  # matches shared/core sc_freq_bin


def _freq_bin(hz: int) -> int:
    return ((hz + FREQ_BIN_HZ // 2) // FREQ_BIN_HZ) * FREQ_BIN_HZ


def _to_epoch(ts: str) -> int | None:
    try:
        return int(_dt.datetime.fromisoformat(ts).timestamp())
    except (ValueError, TypeError):
        return None


def _coarse_cadence(epochs: list[int]) -> dict:
    """Coarse cadence from reception timestamps (the Pi does the canonical version)."""
    ep = sorted(e for e in epochs if e is not None)
    if len(ep) <= 1:
        return {"cadence_class": "seen-once", "period_s": None, "cadence_samples": 0}
    iv = [ep[i] - ep[i - 1] for i in range(1, len(ep)) if ep[i] - ep[i - 1] > 0]
    if not iv:
        return {"cadence_class": "seen-once", "period_s": None, "cadence_samples": 0}
    mean = sum(iv) / len(iv)
    var = sum((x - mean) ** 2 for x in iv) / len(iv)
    cov = (var**0.5) / mean if mean else 1.0
    if mean < 2:
        cls = "near-continuous"
    elif cov < 0.3:
        cls = "periodic"
    elif cov < 0.8:
        cls = "quasi-periodic"
    else:
        cls = "event-driven"
    return {
        "cadence_class": cls,
        "period_s": round(mean, 1) if cls != "event-driven" else None,
        "cadence_samples": len(iv),
    }


def _read_csv(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open("r", newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def build_bundle(place_dir: str | Path, place_name: str | None = None,
                 protocol_map: list[dict] | None = None, generated: str | None = None) -> dict:
    d = Path(place_dir)
    name = place_name or d.name
    log_rows = _read_csv(d / "census_log.csv")

    # group captures into devices by (freq_bin, protocol-or-label)
    groups: dict[tuple, list[dict]] = {}
    for r in log_rows:
        try:
            fb = _freq_bin(int(r.get("freq_hz") or 0))
        except ValueError:
            fb = 0
        key = (fb, (r.get("protocol") or r.get("label") or "").strip())
        groups.setdefault(key, []).append(r)

    identified, needs_id, unknowns = [], [], []
    for (fb, tag), rows in groups.items():
        tss = [r.get("ts_iso", "") for r in rows]
        epochs = [_to_epoch(t) for t in tss]
        entry = {
            "freq_bin": fb,
            "count": len(rows),
            "first_seen": min(tss) if tss else "",
            "last_seen": max(tss) if tss else "",
            "protocol": rows[0].get("protocol", ""),
            "label": next((r.get("label") for r in rows if r.get("label")), ""),
            "fsk_suspected": any(r.get("fsk_suspected") == "1" for r in rows),
            "cadence": _coarse_cadence(epochs),
        }
        if entry["label"] or entry["protocol"]:
            identified.append(entry)
        else:
            needs_id.append(entry)
            if entry["fsk_suspected"]:
                unknowns.append({"freq_bin": fb, "fsk_suspected": True, "count": len(rows)})

    occ = _read_csv(d / "occupancy.csv")
    top = sorted(occ, key=lambda r: float(r.get("occupancy") or 0), reverse=True)[:15]

    return {
        "manifest": {
            "tool": "SubCensus", "source": "csv-place", "place": name, "generated": generated,
            "device_count": len(identified) + len(needs_id), "capture_count": len(log_rows),
        },
        "occupancy_digest": {
            "top_bins": [
                {"freq_hz": int(r["freq_hz"]), "occupancy": float(r["occupancy"]),
                 "peak_rssi": float(r["peak_rssi"])}
                for r in top if float(r.get("occupancy") or 0) > 0
            ],
            "n_bins": len(occ),
        },
        "devices": {"identified": identified, "needs_id": needs_id},
        "unknowns": unknowns,
        "reference_grounding": {
            "ism_bands": ISM_BANDS, "typical_cadences": TYPICAL_CADENCE,
            "protocol_map_slice": (protocol_map or [])[:50],
        },
    }


def render_prompt(bundle: dict) -> str:
    """Paste-able prompt.md (System §8 system-prompt intent: RF/ISM analyst)."""
    m = bundle["manifest"]
    return "\n".join([
        "# SubCensus place analysis",
        "",
        "You are an RF/ISM analyst. Separate confident IDs from guesses; justify each from",
        "freq + modulation + timing + cadence -> device family; flag anomalies neutrally",
        "(including cadence anomalies); propose concrete next captures. Return structured JSON",
        "plus a readable summary.",
        "",
        f"Place: **{m['place']}** ({m['tool']}). Devices: {m['device_count']}, "
        f"captures: {m.get('capture_count', 0)}.",
        "",
        "## Bundle",
        "```json",
        json.dumps(bundle, indent=2, sort_keys=True),
        "```",
    ])
