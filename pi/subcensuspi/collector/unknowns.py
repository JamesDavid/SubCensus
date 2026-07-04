"""Unknown / undecoded capture support (Pi §4, §6).

rtl_433 `-A` (pulse analyzer) + `-S unknown` save `.cu8` IQ snippets for undecoded bursts;
the trigger metadata + pulse summary land in the `unknowns` table for later classification.
IQ is big, so capture is gated behind a config flag AND a disk-space guard (Pi §4, §8).

The IQ *file* save is a live rtl_433 concern (hardware); the metadata/pulse-summary path and
the disk guard are fully fixture-testable here.
"""

from __future__ import annotations

import json
from pathlib import Path

from ..dsp import feature, pulse


def dir_size_bytes(path: str | Path) -> int:
    p = Path(path)
    if not p.exists():
        return 0
    total = 0
    for f in p.rglob("*"):
        if f.is_file():
            try:
                total += f.stat().st_size
            except OSError:
                pass
    return total


def disk_guard_ok(iq_dir: str | Path, max_iq_gb: float) -> bool:
    """True if there's room to capture more IQ (Pi §4 disk guard)."""
    if max_iq_gb <= 0:
        return True
    return dir_size_bytes(iq_dir) < max_iq_gb * (1024**3)


def pulse_summary_from_event(obj: dict) -> str:
    """Compact JSON summary of an undecoded burst: pulse count, modulation, and — when raw
    timing is present — the dominant symbol widths (via the shared DSP)."""
    summary: dict = {}
    if "pulses" in obj:
        summary["n_pulses"] = obj["pulses"] if isinstance(obj["pulses"], int) else None
    if "mod" in obj:
        summary["mod"] = obj["mod"]
    if "codes" in obj:
        summary["codes"] = obj["codes"]
    # if a raw timing array is available, characterize it like the Zero would
    timings = obj.get("timings") or obj.get("pulses_us")
    if isinstance(timings, list) and timings:
        clusters = pulse.cluster(timings, 0.25, 3)
        summary["sym_widths_us"] = [c.center_us for c in clusters]
        freq_hz = int(round(float(obj.get("freq", 0)) * 1_000_000)) if obj.get("freq") else 0
        fv = feature.compute(timings, freq_hz, feature.MOD_OOK)
        summary["n_symbols"] = fv.n_symbols
        summary["est_bitrate"] = fv.est_bitrate
    return json.dumps(summary, separators=(",", ":"), sort_keys=True)
