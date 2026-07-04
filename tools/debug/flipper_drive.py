#!/usr/bin/env python3
"""flipper_drive — inject virtual input events over serial RPC (Debug §2.2, §5).

Parses a navigation sequence and (with --port) sends it to the device. Without --port it
just prints the parsed events (offline verification of the sequence parser).

  python flipper_drive.py "Down Down Ok"
  python flipper_drive.py --port COM3 "down, long:ok, back"    # live (hardware)
"""

from __future__ import annotations

import argparse
import sys

from subcensus_tools.debug.input import parse_sequence
from subcensus_tools.debug.transport import PySerialTransport, RpcSession


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("sequence", help='e.g. "Down Down Ok" or "down, long:ok, back"')
    ap.add_argument("--port", help="serial port to send to (hardware)")
    args = ap.parse_args(argv)

    events = parse_sequence(args.sequence)
    for ev in events:
        print(ev)

    if args.port:
        session = RpcSession(PySerialTransport(args.port))
        session.send_input(events)  # TODO(hw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
