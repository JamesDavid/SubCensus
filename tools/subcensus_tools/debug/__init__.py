"""SubCensus Flipper serial-RPC debug harness (Debug §2.2).

Gives an agent "eyes and hands" on the device over the Flipper's RPC-over-serial:
  - framebuffer.py — 128x64 1-bit screen buffer -> PNG (viewable) + ASCII (cheap text asserts)
  - input.py       — virtual button events (Up/Down/Left/Right/Ok/Back, short/long)
  - log.py         — parse the structured `SC key=value` FURI_LOG convention (Debug §1.3)
  - transport.py   — serial transport (pyserial) + a Fake for hardware-free tests; the
                     protobuf RPC wire format is pinned to the installed firmware (Debug §2.4)

The pure logic (framebuffer decode/render, input model, log parsing) is unit-tested with no
device attached — "build this helper first" (Debug §2.2). Live RPC needs the real Flipper +
the firmware's protobuf definitions and is a hardware step (RF/hardware boundary).
"""
