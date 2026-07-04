#!/usr/bin/env python3
"""flipper_screen — framebuffer screenshot over serial RPC (Debug §2.2, §5).

Decodes the Flipper's 128x64 1-bit framebuffer to PNG (viewable) and/or ASCII (cheap text
asserts). Works fully offline on a captured 1024-byte buffer; live capture over serial needs
the device + the pinned firmware protobuf (TODO(hw), see subcensus_tools/debug/transport.py).

  python flipper_screen.py --from-file frame.bin --ascii
  python flipper_screen.py --from-file frame.bin --png out.png --scale 4
  python flipper_screen.py --port COM3 --png out.png          # live (hardware)
"""

from __future__ import annotations

import argparse
import sys

from subcensus_tools.debug import framebuffer as fb
from subcensus_tools.debug.transport import PySerialTransport, RpcSession


def main(argv: list[str] | None = None) -> int:
    # Half-block glyphs are non-ASCII; force UTF-8 so Windows cp1252 consoles don't choke.
    try:
        sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description=__doc__)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--from-file", help="read a raw 1024-byte framebuffer from this file")
    src.add_argument("--port", help="serial port for a live screenshot (hardware)")
    ap.add_argument("--png", help="write a PNG to this path")
    ap.add_argument("--scale", type=int, default=2, help="PNG upscale factor (default 2)")
    ap.add_argument("--ascii", action="store_true", help="print an ASCII render")
    ap.add_argument("--half", action="store_true", help="print a compact half-block render")
    args = ap.parse_args(argv)

    if args.from_file:
        with open(args.from_file, "rb") as fh:
            buf = fh.read()
    else:
        session = RpcSession(PySerialTransport(args.port))
        buf = session.screenshot()  # TODO(hw)

    pixels = fb.decode_framebuffer(buf)
    if args.png:
        with open(args.png, "wb") as fh:
            fh.write(fb.to_png(pixels, scale=args.scale))
        print(f"wrote {args.png}")
    if args.half:
        print(fb.to_ascii_halfblock(pixels))
    if args.ascii or not (args.png or args.half):
        print(fb.to_ascii(pixels))
    return 0


if __name__ == "__main__":
    sys.exit(main())
