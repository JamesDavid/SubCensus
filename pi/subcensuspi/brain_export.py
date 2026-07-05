"""Export the Pi's labeled data into the shared brain (System §6, §8; Pi §10a).

The Pi is the stronger SEED source for protocol_map (rtl_433's rich decode set) and contributes
user-labeled fingerprints for raw/unknown signals. These exports are fed to the single merge
point, build_signatures.py (System §8), alongside the Zero's — a Pi place and a Zero place are
interchangeable inputs. Fingerprints use the shared feature vector (dsp/, parity-locked).
"""

from __future__ import annotations

import json

from .db import Database, iso_to_epoch
from .dsp import cadence as _cadence


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


def _unknown_cadence(db: Database, place: str, freq_hz: int) -> _cadence.CadenceEstimate:
    """Cadence of an unknown signal from its own reception history — every capture of the same
    (place, freq) burst is one arrival (System §7a). The Pi's continuous corpus makes this the
    cleanest inter-arrival estimate (Pi §5)."""
    secs = [e for e in (iso_to_epoch(t) for t in db.unknown_timestamps(place, freq_hz))
            if e is not None]
    return _cadence.from_timestamps(secs)


def export_fingerprints_from_unknowns(db: Database, place: str | None = None) -> list[dict]:
    """Build fingerprint rows from labeled unknowns that carry a feature vector (active-
    learning loop, System §6). source=user. The dropout-robust cadence_* soft features (§7a)
    are filled from the unknown signal's reception history at that (place, freq)."""
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
        cad = _unknown_cadence(db, u["place"], u["freq_hz"])
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
            "cadence_class": cad.cls,
            "period_s": cad.period_s if cad.period_s > 0 else "",
            "period_regularity": round(cad.regularity, 4) if cad.samples else "",
            "cadence_samples": cad.samples if cad.samples else "",
        })
    return rows


# --- production CLI (Pi §10a): emit the shared-brain exports build_signatures.py ingests ---

def write_exports(db: Database, out_dir, place: str | None = None) -> tuple[int, int]:
    """Write pi_export/fingerprints.csv + protocol_map.csv via the shared brain writers
    (schema-validated on merge). Returns (n_fingerprints, n_protocol_rows)."""
    from pathlib import Path

    from subcensus_tools import brain  # runtime import: only the CLI needs tools on the path

    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    fps = export_fingerprints_from_unknowns(db, place)
    pms = export_protocol_map_from_devices(db, place)
    brain.write_fingerprints(fps, out / "fingerprints.csv")
    brain.write_protocol_map(pms, out / "protocol_map.csv")
    return len(fps), len(pms)


def main(argv: list[str] | None = None) -> int:
    """Emit the Pi's shared-brain exports so build_signatures.py can ingest them (Pi §10a):

        python -m subcensuspi.brain_export --config config.yaml --out pi_export
    """
    import argparse

    from .config import Config

    ap = argparse.ArgumentParser(description=main.__doc__)
    ap.add_argument("--config", required=True, help="YAML config (Pi §8)")
    ap.add_argument("--db", help="override db_path")
    ap.add_argument("--place", help="override the active place (default: config place)")
    ap.add_argument("--out", default="pi_export", help="output dir (default: pi_export)")
    ap.add_argument("--all-places", action="store_true",
                    help="export across all places instead of just the active one")
    args = ap.parse_args(argv)

    cfg = Config.load(args.config)
    db = Database(args.db or cfg.db_path)
    db.refresh_all_cadences()  # keep the catalog's cadence_* current before exporting (§5/§7a)
    place = None if args.all_places else (args.place or cfg.place)
    n_fp, n_pm = write_exports(db, args.out, place)
    db.close()
    print(f"wrote {args.out}/fingerprints.csv ({n_fp} rows) + "
          f"{args.out}/protocol_map.csv ({n_pm} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
