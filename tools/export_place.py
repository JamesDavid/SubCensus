#!/usr/bin/env python3
"""export_place — roll a place into the shared analysis bundle + prompt (System §8).

Accepts a **CSV place folder** — a SubCensusZero place (`/ext/apps_data/subcensuszero/places/
<id>/`) or an Esp SD place — with census_log.csv / occupancy.csv / watchlist.csv. (A Pi place
uses the SQLite path in subcensuspi.export_place; both produce the SAME bundle, so places are
interchangeable inputs, System §8.)

  python tools/export_place.py --place <place_dir> [--out <dir>] \
      [--protocol-map shared/signatures/protocol_map.csv]
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from subcensus_tools import brain
from subcensus_tools.place_bundle import build_bundle, render_prompt


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--place", required=True, help="place folder (census_log.csv + occupancy.csv)")
    ap.add_argument("--out", help="output dir for bundle.json + prompt.md (default: the place)")
    ap.add_argument("--protocol-map", help="protocol_map.csv to include as reference grounding")
    ap.add_argument("--name", help="place display name (default: folder name)")
    args = ap.parse_args(argv)

    pm = brain.read_protocol_map(args.protocol_map) if args.protocol_map else []
    bundle = build_bundle(args.place, place_name=args.name, protocol_map=pm)

    out = Path(args.out or args.place)
    out.mkdir(parents=True, exist_ok=True)
    (out / "bundle.json").write_text(json.dumps(bundle, indent=2, sort_keys=True), encoding="utf-8")
    (out / "prompt.md").write_text(render_prompt(bundle), encoding="utf-8")
    m = bundle["manifest"]
    print(f"wrote {out/'bundle.json'} + prompt.md ({m['device_count']} devices, {m['capture_count']} captures)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
