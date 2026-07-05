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
# Curated protocol_map seed from the Flipper SubGhz protocol registry + rtl_433's device
# catalog. Reference metadata only (name/freq/modulation/device_class) — mind rtl_433's GPL:
# reference parameters, don't copy code (System §8). Keyed by `protocol` (the decoder's name,
# which differs per tool), so both a Flipper name and an rtl_433 model can map to one class.
# APPEND-friendly + hand-verified; not exhaustive but a useful out-of-the-box brain.


def _pm(protocol, friendly_name, device_class, typical_use="", notes=""):
    return {"protocol": protocol, "friendly_name": friendly_name, "device_class": device_class,
            "typical_use": typical_use, "notes": notes}


SEED_PROTOCOL_MAP = [
    # --- Flipper SubGhz registry: gate/garage openers -> garage ---
    _pm("Nice Flo", "Nice FLO gate", "garage", "gate/garage remote", "433.92 OOK fixed code"),
    _pm("Nice FloR-S", "Nice FloR-S gate", "garage", "gate remote", "433.92 OOK rolling code"),
    _pm("CAME", "CAME gate", "garage", "gate remote", "433.92/868 OOK"),
    _pm("CAME TWEE", "CAME TWEE gate", "garage", "gate remote", "433.92 OOK"),
    _pm("CAME Atomo", "CAME Atomo gate", "garage", "gate remote", "433.92 rolling"),
    _pm("Chamberlain", "Chamberlain/LiftMaster", "garage", "garage door", "300-390 OOK"),
    _pm("Linear", "Linear opener", "garage", "gate/garage remote", "300-318 OOK"),
    _pm("Linear Delta 3", "Linear Delta-3", "garage", "gate remote", "310 OOK"),
    _pm("Hormann HSM", "Hormann HSM", "garage", "garage door", "868 rolling"),
    _pm("FAAC SLH", "FAAC SLH", "garage", "gate remote", "433/868 rolling"),
    _pm("BFT Mitto", "BFT Mitto", "garage", "gate remote", "433.92 rolling"),
    _pm("Somfy Telis", "Somfy Telis", "garage", "shutter/gate remote", "433.42 rolling"),
    _pm("Somfy Keytis", "Somfy Keytis", "garage", "gate remote", "433.42 rolling"),
    _pm("Doorhan", "DoorHan", "garage", "gate/barrier remote", "433.92 rolling"),
    _pm("GateTX", "Gate-TX", "garage", "gate remote", "433.92 OOK"),
    _pm("Marantec", "Marantec", "garage", "garage door", "433/868 OOK"),
    _pm("KeeLoq", "KeeLoq rolling", "garage", "gate/car rolling code", "300-868 OOK/FSK"),
    _pm("SMC5326", "SMC5326 remote", "garage", "gate/garage remote", "330 OOK"),
    _pm("Clemsa", "Clemsa", "garage", "gate remote", "433.92 OOK"),
    _pm("Megacode", "Linear MegaCode", "garage", "gate/garage remote", "318 OOK"),
    _pm("Ansonic", "Ansonic", "garage", "gate remote", "433.92/434.075 OOK"),
    # --- generic fixed-code remotes -> remote ---
    _pm("Princeton", "PT2262 remote", "remote", "generic OOK button remote", "433.92 OOK fixed code"),
    _pm("PT2260", "PT2260 remote", "remote", "generic OOK remote", "433.92 OOK"),
    _pm("Holtek", "Holtek HT12x remote", "remote", "generic OOK remote", "433.92 OOK"),
    _pm("Holtek_HT12X", "Holtek HT12x", "remote", "generic OOK remote", "315/433 OOK"),
    _pm("Intertechno_V3", "Intertechno V3", "smart-home", "RF power switch", "433.92 OOK"),
    _pm("Nero Radio", "Nero Radio", "smart-home", "RF switch/remote", "433.92 OOK"),
    _pm("Power Smart", "Power Smart", "smart-home", "RF switch", "433.92 OOK"),
    _pm("Magellan", "Magellan", "pir-motion", "security sensor", "433.92 OOK"),
    _pm("X10", "X10 RF", "smart-home", "home automation", "310/433 OOK"),
    # --- car alarms / fobs -> car-fob ---
    _pm("Star Line", "StarLine alarm", "car-fob", "car alarm remote", "433.92 rolling"),
    _pm("KIA", "KIA fob", "car-fob", "vehicle remote", "433.92 rolling"),
    # --- rtl_433 weather sensors -> weather ---
    _pm("Acurite-Tower", "Acurite tower sensor", "weather", "temp/humidity ~30-60s", "433.92 OOK"),
    _pm("Acurite-5n1", "Acurite 5-in-1", "weather", "weather station", "433.92 OOK"),
    _pm("Acurite-606TX", "Acurite 606TX", "weather", "temperature sensor", "433.92 OOK"),
    _pm("Prologue-TH", "Prologue temp/hum", "weather", "temp/humidity sensor", "433.92 OOK"),
    _pm("Nexus-TH", "Nexus temp/hum", "weather", "temp/humidity sensor", "433.92 OOK"),
    _pm("LaCrosse-TX", "LaCrosse TX", "weather", "temp/humidity sensor", "433.92 OOK"),
    _pm("LaCrosse-TX141THBv2", "LaCrosse TX141TH", "weather", "temp/humidity sensor", "433.92 OOK"),
    _pm("Oregon-Scientific", "Oregon Scientific", "weather", "weather sensor", "433.92 OOK"),
    _pm("Ambient-Weather", "Ambient Weather", "weather", "weather sensor", "915 OOK"),
    _pm("Fineoffset-WH2", "Fine Offset WH2", "weather", "temp/humidity sensor", "433.92 OOK"),
    _pm("Fineoffset-WH24", "Fine Offset WH24", "weather", "weather station", "433/915 FSK"),
    _pm("Bresser-3CH", "Bresser 3CH", "weather", "temp/humidity sensor", "433.92 OOK"),
    _pm("Bresser-5in1", "Bresser 5-in-1", "weather", "weather station", "868 FSK"),
    _pm("TFA-Twin-Plus", "TFA Twin Plus", "weather", "temp/humidity sensor", "433.92 OOK"),
    _pm("GT-WT02", "Globaltronics GT-WT", "weather", "temp/humidity sensor", "433.92 OOK"),
    _pm("inFactory-TH", "inFactory temp/hum", "weather", "temp/humidity sensor", "433.92 OOK"),
    # --- TPMS -> tpms ---
    _pm("Schrader", "Schrader TPMS", "tpms", "tire pressure, motion-triggered", "315/433 FSK"),
    _pm("Schrader-EG53MA4", "Schrader EG53MA4 TPMS", "tpms", "tire pressure", "315 FSK"),
    _pm("Toyota", "Toyota TPMS", "tpms", "tire pressure", "315/433 FSK"),
    _pm("Ford", "Ford TPMS", "tpms", "tire pressure", "315/433 FSK"),
    _pm("Renault", "Renault TPMS", "tpms", "tire pressure", "433 FSK"),
    _pm("Citroen", "Citroen TPMS", "tpms", "tire pressure", "433 FSK"),
    _pm("Abarth-124Spider", "Abarth 124 TPMS", "tpms", "tire pressure", "433 FSK"),
    # --- utility meters -> energy/water-gas meter ---
    _pm("ERT-SCM", "Utility meter (ERT SCM)", "energy-meter", "electric meter", "915 FSK"),
    _pm("ERT-IDM", "Utility meter (ERT IDM)", "energy-meter", "electric meter", "915 FSK"),
    _pm("Neptune-R900", "Neptune R900", "water/gas-meter", "water meter", "915 FSK"),
    # --- security / doorbell / motion ---
    _pm("Honeywell", "Honeywell Security", "pir-motion", "door/window/motion sensor", "345 FSK"),
    _pm("Honeywell-Security", "Honeywell Security", "pir-motion", "security sensor", "345 FSK"),
    _pm("Interlogix", "Interlogix/GE", "pir-motion", "door/window/motion sensor", "319.5 OOK"),
    _pm("X10-Security", "X10 Security", "pir-motion", "security sensor", "310 OOK"),
    _pm("Secplus-v1", "Security+ 1.0", "garage", "garage door (Sec+ 1.0)", "310/390 OOK"),
    _pm("Secplus-v2", "Security+ 2.0", "garage", "garage door (Sec+ 2.0)", "310/315/390 FSK"),
]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--signatures-dir", required=True, help="global signatures/ dir (System §4)")
    ap.add_argument("--fingerprints", nargs="*", default=[],
                    help="fingerprint CSVs to merge (Zero place folders and/or Pi exports)")
    ap.add_argument("--protocol-map", nargs="*", default=[],
                    help="extra protocol_map CSVs to merge in")
    ap.add_argument("--places", nargs="*", default=[],
                    help="place folders to run passive field-map discovery over (System §7b/§8)")
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

    # field_maps: passive differential + checksum discovery over each place's capture corpus
    # (System §7b/§8). PROPOSES `.fmap` entries (source=proposed) — never auto-committed.
    from subcensus_tools import fieldmap
    proposals = []
    for pd in args.places:
        proposals += fieldmap.discover_place(Path(pd))
    if proposals:
        written = fieldmap.write_proposals(sig, proposals)
        print(f"field_maps: {len(written)} proposed (source=proposed, user confirms) -> {sig / 'field_maps'}")
    else:
        print("field_maps: none proposed (need >=2 same-freq captures per device; System §7b)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
