"""Export the Pi's labeled data into the shared brain (System §6, §8; Pi §10a).

The Pi is the stronger SEED source for protocol_map (rtl_433's rich decode set) and contributes
user-labeled fingerprints for raw/unknown signals. These exports are fed to the single merge
point, build_signatures.py (System §8), alongside the Zero's — a Pi place and a Zero place are
interchangeable inputs. Fingerprints use the shared feature vector (dsp/, parity-locked).
"""

from __future__ import annotations

import json

from .db import Database


def export_protocol_map_from_devices(db: Database, place: str | None = None) -> list[dict]:
    """Propose protocol_map rows from labeled decoded devices (model -> name/class).
    The Pi's rtl_433 decode makes it the better protocol_map seed (Pi §10a)."""
    rows: list[dict] = []
    seen: set[str] = set()
    for d in db.list_devices(place):
        cls = d["device_class"]
        if not cls or d["model"] in seen:
            continue
        seen.add(d["model"])
        rows.append({
            "protocol": d["model"],
            "friendly_name": d["label"] or d["model"],
            "device_class": cls,
            "typical_use": d["room"] or "",
            "notes": "seeded from Pi label",
        })
    return rows


def export_fingerprints_from_unknowns(db: Database, place: str | None = None) -> list[dict]:
    """Build fingerprint rows from labeled unknowns that carry a feature vector (active-
    learning loop, System §6). source=user."""
    rows: list[dict] = []
    for u in db.list_unknowns(place):
        cls = u["device_class"]
        if not cls or not u["pulse_summary"]:
            continue
        try:
            summary = json.loads(u["pulse_summary"])
        except (TypeError, json.JSONDecodeError):
            continue
        fv = summary.get("fv")
        if not fv:
            continue
        syms = fv.get("sym_dur_us", [])
        rows.append({
            "id": "",  # assigned on merge
            "freq_bin": fv.get("freq_bin", 0),
            "modulation": fv.get("modulation", "OOK"),
            "sym_dur_us_1": syms[0] if len(syms) > 0 else "",
            "sym_dur_us_2": syms[1] if len(syms) > 1 else "",
            "sym_dur_us_3": syms[2] if len(syms) > 2 else "",
            "n_symbols": fv.get("n_symbols", 0),
            "est_bitrate": fv.get("est_bitrate", 0),
            "preamble_len": fv.get("preamble_len", 0),
            "repeat_count": fv.get("repeat_count", 0),
            "device_name": u["label"] or "",
            "device_class": cls,
            "source": "user",
            "cadence_class": "",
            "period_s": "",
            "period_regularity": "",
            "cadence_samples": "",
        })
    return rows
