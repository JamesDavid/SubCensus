"""Flipper `.sub` RAW parse/encode — Python port of shared/core/sc_sub.c (Zero §5.1, §5.4).

The Pi normally derives features from IQ/pulse via rtl_433, but it also reads `.sub` fixtures
so the DSP port can be parity-checked against the C core on the same inputs.
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class SubFile:
    frequency: int = 0
    preset: str = ""
    protocol: str = ""
    timings: list[int] = field(default_factory=list)


def parse(text: str) -> SubFile:
    """Parse `.sub` text. RAW_Data values are signed us durations (+=mark, -=space)."""
    sub = SubFile()
    for line in text.splitlines():
        if line.startswith("Frequency:"):
            sub.frequency = int(line.split(":", 1)[1].strip() or "0")
        elif line.startswith("Preset:"):
            sub.preset = line.split(":", 1)[1].strip()
        elif line.startswith("Protocol:"):
            sub.protocol = line.split(":", 1)[1].strip()
        elif line.startswith("RAW_Data:"):
            for tok in line[len("RAW_Data:"):].split():
                try:
                    sub.timings.append(int(tok))
                except ValueError:
                    break
    return sub


def encode(sub: SubFile, values_per_line: int = 512) -> str:
    preset = sub.preset or "FuriHalSubGhzPresetOok650Async"
    protocol = sub.protocol or "RAW"
    out = [
        "Filetype: Flipper SubGhz RAW File",
        "Version: 1",
        f"Frequency: {sub.frequency}",
        f"Preset: {preset}",
        f"Protocol: {protocol}",
    ]
    for i in range(0, len(sub.timings), values_per_line):
        chunk = sub.timings[i : i + values_per_line]
        out.append("RAW_Data: " + " ".join(str(v) for v in chunk))
    return "\n".join(out) + "\n"
