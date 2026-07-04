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


def rtl_power_rows() -> list[str]:
    """Synthetic rtl_power CSV (Pi §3 occupancy pass input). Two bands, 4 time passes.
    Format: date, time, Hz_low, Hz_high, Hz_step, samples, dbm, dbm, ...
    433.42-434.42 MHz @ 100 kHz (10 bins): the 433.92 bin is hot every pass (occ ~1.0).
    314.5-315.5 MHz @ 100 kHz (10 bins): the 315.0 bin is hot on 2 of 4 passes (occ ~0.5)."""
    rows: list[str] = []
    times = ["12:00:00", "12:05:00", "12:10:00", "12:15:00"]
    for pi, t in enumerate(times):
        # band A: 433.42..434.42, hot bin index 5 (~433.92)
        a = [-96, -95, -97, -96, -95, -55, -96, -97, -95, -96]
        rows.append(f"2026-07-04, {t}, 433420000, 434420000, 100000, 100, "
                    + ", ".join(str(v) for v in a))
        # band B: 314.5..315.5, hot bin index 5 (~315.0) only on passes 0 and 2
        b = [-98, -97, -98, -97, -98, (-58 if pi % 2 == 0 else -97), -98, -97, -98, -97]
        rows.append(f"2026-07-04, {t}, 314500000, 315500000, 100000, 100, "
                    + ", ".join(str(v) for v in b))
    return rows


def write_rtl_power(name: str) -> None:
    d = HERE / "rtl_power"
    d.mkdir(parents=True, exist_ok=True)
    (d / name).write_text("\n".join(rtl_power_rows()) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote rtl_power/{name}")


def fieldmap_corpus_rows() -> list[str]:
    """rtl_433-style events for one device with a raw `data` hex payload that varies across
    receptions (System §7b differential corpus): byte0 static id, byte1 counter, byte2 sensor
    value (tracks temperature_C for ground-truth correlation), byte3 = XOR checksum. 8 frames
    60 s apart."""
    import json as _json

    rows = []
    for i in range(8):
        temp = 20 + i // 2  # slow-varying (changes every 2 frames), unlike the per-frame counter
        b0, b1, b2 = 0xA5, i, temp
        b3 = b0 ^ b1 ^ b2
        data = f"{b0:02x}{b1:02x}{b2:02x}{b3:02x}"
        t = f"12:{i:02d}:00"
        rows.append(_json.dumps({
            "time": f"2026-07-04T{t}", "model": "RawSensor", "id": 7, "freq": 433.92,
            "rssi": -62.0, "snr": 11.0, "data": data, "temperature_C": temp,
        }, separators=(",", ":")))
    return rows


def write_fieldmap_corpus(name: str) -> None:
    d = HERE / "rtl433"
    d.mkdir(parents=True, exist_ok=True)
    (d / name).write_text("\n".join(fieldmap_corpus_rows()) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote rtl433/{name}")


def main() -> None:
    # A remote press = the same frame repeated 5x (Zero §7 repeat suppression territory).
    remote = ook_remote_frame()
    write_sub("synth_ook_remote_433.sub", 433920000,
              "FuriHalSubGhzPresetOok650Async", [remote] * 5)

    # A single weather beacon frame (one reception; cadence needs the multi-capture corpus).
    write_sub("synth_weather_ook_433.sub", 433920000,
              "FuriHalSubGhzPresetOok650Async", [weather_frame(1, 213)])

    # rtl_power occupancy sweep (Pi §3 heatmap pass input).
    write_rtl_power("home_sweep.csv")

    # field-map discovery corpus (System §7b differential + checksum + ground-truth).
    write_fieldmap_corpus("fieldmap_corpus.jsonl")


if __name__ == "__main__":
    main()
