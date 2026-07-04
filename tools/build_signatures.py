#!/usr/bin/env python3
"""build_signatures.py — assemble/merge the classification brain (System §8).

The single merge point for the global signatures/ brain:
  - seeds protocol_map.csv from a curated slice of the Flipper SubGhz registry + rtl_433's
    device catalog (freq/modulation/bit-length/fields as REFERENCE metadata; rtl_433 is GPL —
    reference parameters, don't copy code into the FAP).
  - merges user-labeled fingerprints from BOTH tools (Zero place folders + Pi exports) with
    dedup (System §6 active-learning loop).
  - (M9 / Pi §9) runs the passive field-map discovery over each device's capture corpus to
    PROPOSE field_maps/ entries. **Proposes, never auto-commits** derived structures.

Accepts a SubCensusZero place folder or a SubCensusPi export as fingerprint sources; both are
interchangeable inputs.

  python build_signatures.py --signatures-dir <dir> \
      --fingerprints zero_place/fingerprints.csv pi_export/fingerprints.csv
"""

from __future__ import annotations

import argparse
from pathlib import Path

from subcensus_tools import brain

# Curated protocol_map seed (reference metadata). Real builds extend this from the Flipper
# SubGhz registry + rtl_433 -R catalog; kept small + hand-verified here.
SEED_PROTOCOL_MAP = [
    {"protocol": "Princeton", "friendly_name": "PT2262 remote", "device_class": "remote",
     "typical_use": "generic OOK button remote", "notes": "433.92 OOK, fixed code"},
    {"protocol": "Nice-Flo", "friendly_name": "Nice FLO gate", "device_class": "garage",
     "typical_use": "gate/garage remote", "notes": "433.92 OOK fixed code"},
    {"protocol": "CAME", "friendly_name": "CAME gate", "device_class": "garage",
     "typical_use": "gate remote", "notes": "433.92 OOK"},
    {"protocol": "Acurite-Tower", "friendly_name": "Acurite tower sensor", "device_class": "weather",
     "typical_use": "temp/humidity beacon ~30-60s", "notes": "433.92 OOK, rtl_433"},
    {"protocol": "Prologue-TH", "friendly_name": "Prologue temp/hum", "device_class": "weather",
     "typical_use": "temp/humidity sensor", "notes": "433.92 OOK, rtl_433"},
    {"protocol": "Schrader", "friendly_name": "Schrader TPMS", "device_class": "tpms",
     "typical_use": "tire pressure, motion-triggered", "notes": "315/433 FSK, rtl_433"},
    {"protocol": "ERT-SCM", "friendly_name": "Utility meter (ERT SCM)", "device_class": "energy-meter",
     "typical_use": "electric/utility meter", "notes": "915 FSK, rtl_433"},
]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--signatures-dir", required=True, help="global signatures/ dir (System §4)")
    ap.add_argument("--fingerprints", nargs="*", default=[],
                    help="fingerprint CSVs to merge (Zero place folders and/or Pi exports)")
    ap.add_argument("--protocol-map", nargs="*", default=[],
                    help="extra protocol_map CSVs to merge in")
    ap.add_argument("--no-seed", action="store_true", help="don't add the curated protocol_map seed")
    args = ap.parse_args(argv)

    sig = Path(args.signatures_dir)
    sig.mkdir(parents=True, exist_ok=True)
    pm_path = sig / "protocol_map.csv"
    fp_path = sig / "fingerprints.csv"

    # protocol_map: existing + seed + extras
    pm_sources = [brain.read_protocol_map(pm_path)]
    if not args.no_seed:
        pm_sources.append(SEED_PROTOCOL_MAP)
    pm_sources += [brain.read_protocol_map(p) for p in args.protocol_map]
    protocol_map = brain.merge_protocol_map(pm_sources)
    brain.write_protocol_map(protocol_map, pm_path)

    # fingerprints: existing + each source, deduped
    fp_sources = [brain.read_fingerprints(fp_path)]
    fp_sources += [brain.read_fingerprints(p) for p in args.fingerprints]
    fingerprints = brain.merge_fingerprints(fp_sources)
    brain.write_fingerprints(fingerprints, fp_path)

    print(f"protocol_map: {len(protocol_map)} rows -> {pm_path}")
    print(f"fingerprints: {len(fingerprints)} rows -> {fp_path}")
    print("field_maps: proposed only, never auto-committed (System §7b) — TODO(M9 differential)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
