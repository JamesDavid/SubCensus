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


def place_iq_dir(places_dir: str | Path, place: str) -> Path:
    """Per-place captured-IQ directory (§9a): ``places_dir/<place>/iq``."""
    return Path(places_dir) / place / "iq"


def iq_path_for(places_dir: str | Path, place: str, freq_hz: int, ts: str = "") -> Path:
    """Derive the place-scoped ``.cu8`` path for a captured unknown burst (§9a).

    Path derivation only — the actual IQ file WRITE is a live rtl_433 concern (TODO(hw)); this
    is what the collector records in ``unknowns.iq_path`` so the dashboard can find the snippet.
    """
    stamp = ts.replace(":", "").replace("-", "").replace("T", "_") if ts else ""
    name = f"unk_{freq_hz}{('_' + stamp) if stamp else ''}.cu8"
    return place_iq_dir(places_dir, place) / name


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
    # if a raw timing array is available, characterize it like the Zero would and embed the
    # full feature vector so a confirmed label can seed the shared brain (System §6, M7).
    timings = obj.get("timings") or obj.get("pulses_us")
    if isinstance(timings, list) and timings:
        clusters = pulse.cluster(timings, 0.25, 3)
        summary["sym_widths_us"] = [c.center_us for c in clusters]
        freq_hz = int(round(float(obj.get("freq", 0)) * 1_000_000)) if obj.get("freq") else 0
        mod = feature.MOD_OOK
        if str(obj.get("mod", "")).upper() in ("FSK", "GFSK", "2-FSK"):
            mod = feature.MOD_2FSK
        fv = feature.compute(timings, freq_hz, mod)
        summary["n_symbols"] = fv.n_symbols
        summary["est_bitrate"] = fv.est_bitrate
        summary["fv"] = {
            "freq_bin": fv.freq_bin,
            "modulation": fv.modulation,
            "sym_dur_us": fv.sym_dur_us,
            "n_symbols": fv.n_symbols,
            "est_bitrate": fv.est_bitrate,
            "preamble_len": fv.preamble_len,
            "repeat_count": fv.repeat_count,
        }
    return json.dumps(summary, separators=(",", ":"), sort_keys=True)
