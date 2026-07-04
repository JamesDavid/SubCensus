"""rtl_433 process management + recorded-input replay (Pi §3, §4; Debug §4).

Live capture spawns rtl_433 and streams its JSON stdout. When rtl_433 isn't installed (e.g.
Windows dev) or no dongle is attached, the collector is driven from recorded JSON instead —
the same decode->catalog path, deterministically, with no hardware (Debug §4). Live dongle
behaviour (gain/ppm/real reception) is the only part that needs hardware — TODO(hw).
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path
from typing import Iterator

from ..config import DongleConfig


def rtl433_available() -> bool:
    return shutil.which("rtl_433") is not None


def build_argv(dongle: DongleConfig, extra: list[str] | None = None) -> list[str]:
    """Baseline decode stream (Pi §4). Multi-freq hop when >1 freq configured."""
    argv = ["rtl_433", "-F", "json", "-M", "time:iso:tz", "-M", "level", "-M", "protocol"]
    if dongle.serial:
        argv += ["-d", f":{dongle.serial}"]
    for f in dongle.freqs:
        argv += ["-f", str(f)]
    if len(dongle.freqs) > 1:
        argv += ["-H", str(dongle.hop_seconds)]
    if dongle.gain not in ("auto", "", None):
        argv += ["-g", str(dongle.gain)]
    if dongle.ppm:
        argv += ["-p", str(dongle.ppm)]
    argv += extra or []
    return argv


def replay_file(path: str | Path) -> Iterator[str]:
    """Yield recorded rtl_433 JSON lines from a .jsonl fixture (no hardware)."""
    with Path(path).open("r", encoding="utf-8") as fh:
        for line in fh:
            if line.strip():
                yield line


def replay_cu8(path: str | Path, extra: list[str] | None = None) -> Iterator[str]:
    """Replay a recorded .cu8/.ook IQ file through rtl_433 -r (needs the rtl_433 binary).
    Skipped in CI when rtl_433 is absent (raises RuntimeError)."""
    if not rtl433_available():  # pragma: no cover - environment dependent
        raise RuntimeError("rtl_433 not installed; use replay_file with recorded JSON instead")
    argv = ["rtl_433", "-r", str(path), "-F", "json", "-M", "level", "-M", "protocol"] + (extra or [])
    proc = subprocess.Popen(argv, stdout=subprocess.PIPE, text=True)  # pragma: no cover
    assert proc.stdout is not None
    for line in proc.stdout:
        if line.strip():
            yield line
    proc.wait()


def stream_live(dongle: DongleConfig) -> Iterator[str]:  # pragma: no cover - needs hardware
    """Spawn rtl_433 on a real dongle and stream JSON. TODO(hw): needs a dongle."""
    if not rtl433_available():
        raise RuntimeError("rtl_433 not installed")
    proc = subprocess.Popen(build_argv(dongle), stdout=subprocess.PIPE, text=True)
    assert proc.stdout is not None
    try:
        for line in proc.stdout:
            if line.strip():
                yield line
    finally:
        proc.terminate()
