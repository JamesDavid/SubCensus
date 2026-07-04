#!/usr/bin/env python3
"""Deterministic fixture generator (Debug §1.2).

Writes synthetic `.sub` RAW captures with KNOWN structure so the core tests can assert
exact classifier/feature/cadence/field-map results (golden outputs live next to the
fixtures or inline in the tests). Reproducible: re-running regenerates byte-identical files.

RF boundary (CLAUDE.md / Debug §7): these are synthetic stand-ins for a radio — they prove
the *processing* path, not the physics. Real device recordings can be dropped alongside.

Usage:  python test/fixtures/generate.py
"""

from __future__ import annotations

from pathlib import Path

HERE = Path(__file__).resolve().parent
SUB = HERE / "sub"

SUB_HEADER = (
    "Filetype: Flipper SubGhz RAW File\n"
    "Version: 1\n"
    "Frequency: {freq}\n"
    "Preset: {preset}\n"
    "Protocol: RAW\n"
)


def write_sub(name: str, freq: int, preset: str, frames: list[list[int]]) -> None:
    SUB.mkdir(parents=True, exist_ok=True)
    text = SUB_HEADER.format(freq=freq, preset=preset)
    for frame in frames:
        text += "RAW_Data: " + " ".join(str(v) for v in frame) + "\n"
    (SUB / name).write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote sub/{name}  ({len(frames)} frames)")


def ook_remote_frame() -> list[int]:
    """PT2262-style OOK remote: 8-short preamble, 12 bits (short=350/long=1050), sync gap.
    Bit '1' = (+1050,-350), bit '0' = (+350,-1050). First bit '1' so the preamble run ends
    cleanly at 8; last bit '1' so the frame ends on -350 before the sync gap."""
    SHORT, LONG, SYNC = 350, 1050, -10850
    f = [SHORT, -SHORT] * 4  # 8-pulse preamble of |350|
    bits = [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1]
    for b in bits:
        if b:
            f += [LONG, -SHORT]
        else:
            f += [SHORT, -LONG]
    f += [SYNC]
    return f


def weather_frame(counter: int, temp_c10: int) -> list[int]:
    """Weather-sensor-style OOK: PWM bits carrying id/counter/temp. Not a real protocol —
    just deterministic structure (used for cadence/differential corpora)."""
    SHORT, LONG, SYNC = 500, 1000, -30000
    f = [SHORT, -SHORT] * 6  # preamble |500| x12
    payload = [0xA5, counter & 0xFF, temp_c10 & 0xFF]
    for byte in payload:
        for i in range(7, -1, -1):
            bit = (byte >> i) & 1
            if bit:
                f += [LONG, -SHORT]
            else:
                f += [SHORT, -LONG]
    f += [SYNC]
    return f


def main() -> None:
    # A remote press = the same frame repeated 5x (Zero §7 repeat suppression territory).
    remote = ook_remote_frame()
    write_sub("synth_ook_remote_433.sub", 433920000,
              "FuriHalSubGhzPresetOok650Async", [remote] * 5)

    # A single weather beacon frame (one reception; cadence needs the multi-capture corpus).
    write_sub("synth_weather_ook_433.sub", 433920000,
              "FuriHalSubGhzPresetOok650Async", [weather_frame(1, 213)])


if __name__ == "__main__":
    main()
