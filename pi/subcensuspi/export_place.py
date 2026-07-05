"""export_place — roll a Pi place into the shared analysis bundle (System §8).

Queries a place's SQLite into a compact, structured bundle: manifest, occupancy digest,
device roll-up (Identified vs Needs-ID) with decoded IDs, match candidate, and each device's
cadence (System §7a); feature vectors for unknowns; reference grounding (ISM band -> typical
device + typical cadences per class + protocol_map slice). Emits a paste-able prompt.md too.

A Zero place folder and a Pi place are interchangeable inputs to the shared tools (System §8);
this is the Pi realization (SQLite query). Deterministic — the caller supplies any timestamp.
"""

from __future__ import annotations

import datetime as _dt
import json
from pathlib import Path

from . import fieldmap, taxonomy
from .db import Database
from .dsp import cadence

# Coverage-gap thresholds (occupancy digest, System §8/§9): a large unmonitored span between
# active bins, plus ISM segments with no occupancy at all.
_COVERAGE_GAP_HZ = 1_000_000
_ISM_CENTERS = {"315 MHz": 315_000_000, "433.92 MHz": 433_920_000,
                "868 MHz": 868_000_000, "915 MHz": 915_000_000}
_ISM_NEAR_HZ = 2_000_000

# Reference grounding (System §8): ISM band -> typical devices + typical cadences per class.
ISM_BANDS = {
    "315 MHz": ["car-fob", "tpms", "pir-motion", "remote"],
    "433.92 MHz": ["remote", "weather", "tpms", "doorbell", "garage", "smart-home"],
    "868 MHz": ["weather", "energy-meter", "water/gas-meter", "smart-home"],
    "915 MHz": ["energy-meter", "water/gas-meter", "weather", "beacon"],
}
TYPICAL_CADENCE = {
    "weather": "periodic ~30-60 s",
    "energy-meter": "periodic ~5 min / hourly",
    "water/gas-meter": "periodic ~5 min / hourly",
    "tpms": "motion-triggered then silent (event-driven)",
    "pir-motion": "event-driven",
    "doorbell": "event-driven / one-shot",
    "remote": "event-driven / one-shot",
}


def _to_epoch(ts: str) -> int | None:
    try:
        return int(_dt.datetime.fromisoformat(ts).timestamp())
    except (ValueError, TypeError):
        return None


def _device_cadence(db: Database, device_id: str) -> dict:
    epochs = [e for e in (_to_epoch(t) for t in db.device_event_timestamps(device_id)) if e is not None]
    est = cadence.from_timestamps(epochs)
    return {
        "cadence_class": est.cls,
        "period_s": round(est.period_s, 2) if est.period_s else None,
        "period_regularity": round(est.regularity, 3),
        "cadence_samples": est.samples,
    }


def _field_discovery(db: Database, device_id: str) -> dict | None:
    """Deterministic §7b evidence for a needs-ID device: the differential segment classes +
    trailing-checksum guess over its reconstructed frame corpus (via the passive Pi field-map
    discovery). RX-only, no TX (System §7b). Grounds the model's field_maps proposal (System §8).
    Returns None when there aren't >=2 alignable frames the differential needs."""
    prop = fieldmap.analyze_device(db, device_id)
    if prop is None:
        return None
    return {
        "n_frames": prop.n_frames,
        "n_bytes": prop.n_bytes,
        "segments": [
            {"name": s.name, "start_bit": s.start_bit, "length": s.length, "class": s.cls}
            for s in prop.fields
        ],
        "checksum": prop.checksum,
    }


def _coverage_gaps(active_freqs: list[int]) -> list[dict]:
    """Simple occupancy coverage gaps (System §8): large unmonitored spans between active bins,
    plus ISM segments with no occupancy at all."""
    gaps: list[dict] = []
    fs = sorted({int(f) for f in active_freqs})
    for a, b in zip(fs, fs[1:]):
        if b - a > _COVERAGE_GAP_HZ:
            gaps.append({"type": "unmonitored-span", "from_hz": a, "to_hz": b, "span_hz": b - a})
    for band, center in _ISM_CENTERS.items():
        if not any(abs(f - center) <= _ISM_NEAR_HZ for f in fs):
            gaps.append({"type": "band-no-occupancy", "band": band})
    return gaps


def build_bundle(db: Database, place: str, *, occupancy_csv: str | Path | None = None,
                 protocol_map: list[dict] | None = None, generated: str | None = None) -> dict:
    protocol_map = protocol_map or []
    pm_by_proto = {r.get("protocol"): r for r in protocol_map}

    identified, needs_id = [], []
    for d in db.list_devices(place):
        entry = {
            "device_id": d["device_id"],
            "model": d["model"],
            "decoded_id": d["dev_id"],
            "channel": d["channel"],
            "typical_freq_hz": d["typical_freq_hz"],
            "count": d["count"],
            "avg_snr": d["avg_snr"],
            "first_seen": d["first_seen"],
            "last_seen": d["last_seen"],
            "label": d["label"],
            "device_class": d["device_class"],
            "cadence": _device_cadence(db, d["device_id"]),
        }
        cand = pm_by_proto.get(d["model"])
        if cand:
            entry["match_candidate"] = {
                "name": cand.get("friendly_name"),
                "device_class": cand.get("device_class"),
                "match_source": "decoder",
            }
        if d["device_class"] or d["label"]:
            identified.append(entry)
        else:
            # ground the field_maps proposal for a needs-ID device in the §7b differential +
            # checksum guess over its capture corpus (System §8), when frames are reconstructable.
            fd = _field_discovery(db, d["device_id"])
            if fd is not None:
                entry["field_discovery"] = fd
            needs_id.append(entry)

    unknowns = []
    for u in db.list_unknowns(place):
        fv = None
        if u["pulse_summary"]:
            try:
                fv = json.loads(u["pulse_summary"]).get("fv")
            except (TypeError, json.JSONDecodeError):
                fv = None
        unknowns.append({
            "id": u["id"], "freq_hz": u["freq_hz"], "ts": u["ts"],
            "device_class": u["device_class"], "feature_vector": fv,
        })

    bundle = {
        "manifest": {
            "tool": "SubCensusPi",
            "place": place,
            "generated": generated,
            "device_count": len(identified) + len(needs_id),
            "unknown_count": len(unknowns),
        },
        "occupancy_digest": _occupancy_digest(occupancy_csv),
        "devices": {"identified": identified, "needs_id": needs_id},
        "unknowns": unknowns,
        "reference_grounding": {
            "ism_bands": ISM_BANDS,
            "typical_cadences": TYPICAL_CADENCE,
            "protocol_map_slice": protocol_map[:50],
        },
    }
    return bundle


def _occupancy_digest(occupancy_csv: str | Path | None) -> dict:
    if not occupancy_csv or not Path(occupancy_csv).exists():
        return {"top_bins": [], "coverage": "no occupancy pass for this place"}
    from .occupancy_pass import read_occupancy_csv

    bins = sorted(read_occupancy_csv(occupancy_csv), key=lambda b: b.occupancy, reverse=True)
    top = [
        {"freq_hz": b.freq_hz, "occupancy": b.occupancy, "peak_rssi": b.peak_rssi,
         "crossings": b.crossings}
        for b in bins[:15] if b.occupancy > 0
    ]
    active_freqs = [b.freq_hz for b in bins if b.occupancy > 0]
    return {"top_bins": top, "n_bins": len(bins), "coverage_gaps": _coverage_gaps(active_freqs)}


def render_prompt(bundle: dict) -> str:
    """Paste-able prompt.md (System §8 system-prompt intent: RF/ISM analyst)."""
    m = bundle["manifest"]
    lines = [
        "# SubCensus place analysis",
        "",
        "You are an RF/ISM analyst. Separate confident IDs from guesses; justify each from",
        "freq + modulation + timing + cadence -> device family; flag anomalies neutrally",
        "(including cadence anomalies); propose concrete next captures. Return structured JSON",
        "plus a readable summary.",
        "",
        f"Place: **{m['place']}** ({m['tool']}). Devices: {m['device_count']}, "
        f"unknowns: {m['unknown_count']}.",
        "",
        "## Bundle",
        "```json",
        json.dumps(bundle, indent=2, sort_keys=True),
        "```",
    ]
    return "\n".join(lines)


def export_place(db: Database, place: str, out_dir: str | Path, *,
                 occupancy_csv: str | Path | None = None, protocol_map: list[dict] | None = None,
                 generated: str | None = None) -> Path:
    """Write bundle.json + prompt.md for a place. Returns the output dir."""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    bundle = build_bundle(db, place, occupancy_csv=occupancy_csv,
                          protocol_map=protocol_map, generated=generated)
    (out / "bundle.json").write_text(json.dumps(bundle, indent=2, sort_keys=True), encoding="utf-8")
    (out / "prompt.md").write_text(render_prompt(bundle), encoding="utf-8")
    return out


def taxonomy_classes() -> list[str]:
    return taxonomy.class_ids()
