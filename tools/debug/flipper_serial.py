#!/usr/bin/env python3
"""flipper_serial — capture + parse FURI_LOG output (Debug §2.2, §1.3, §5).

Parses the structured `SC key=value` log convention into events for text-based assertions.
Works on a captured log file; live streaming needs the device (`ufbt cli` / RPC).

  python flipper_serial.py --from-file run.log
  python flipper_serial.py --from-file run.log --where scene=review action=label
  python flipper_serial.py --port COM3                         # live stream (hardware)
"""

from __future__ import annotations

import argparse
import sys

from subcensus_tools.debug.log import find, parse_log


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--from-file", help="parse a captured log file")
    src.add_argument("--port", help="serial port to stream logs from (hardware)")
    ap.add_argument(
        "--where", nargs="*", default=[],
        help="assert an event exists with these key=value fields; exit 1 if not found",
    )
    args = ap.parse_args(argv)

    if args.port:
        print("TODO(hw): live FURI_LOG streaming needs the device (ufbt cli / RPC).",
              file=sys.stderr)
        return 2

    with open(args.from_file, "r", encoding="utf-8", errors="replace") as fh:
        events = parse_log(fh.read())

    for ev in events:
        print(ev.fields)

    if args.where:
        match = dict(kv.split("=", 1) for kv in args.where)
        if find(events, **match) is None:
            print(f"NOT FOUND: {match}", file=sys.stderr)
            return 1
        print(f"OK: found {match}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
