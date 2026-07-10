"""rtl_433 process management + recorded-input replay (Pi §3, §4; Debug §4).

Live capture spawns rtl_433 and streams its JSON stdout. When rtl_433 isn't installed (e.g.
Windows dev) or no dongle is attached, the collector is driven from recorded JSON instead —
the same decode->catalog path, deterministically, with no hardware (Debug §4). Live dongle
behaviour (gain/ppm/real reception) is the only part that needs hardware — TODO(hw).
"""

from __future__ import annotations

import logging
import shutil
import subprocess
import time
from pathlib import Path
from typing import Callable, Iterator

from ..config import DongleConfig

log = logging.getLogger("subcensuspi.rtl433")


def rtl433_available() -> bool:
    return shutil.which("rtl_433") is not None


def supervise_stream(
    factory: Callable[[], Iterator[str]],
    *,
    max_relaunches: int | None = None,
    backoff_s: float = 1.0,
    backoff_max_s: float = 30.0,
    sleep: Callable[[float], None] = time.sleep,
    on_relaunch: Callable[[int, float], None] | None = None,
) -> Iterator[str]:
    """Respawn-with-backoff wrapper around a line stream (Pi §9: "Collector should relaunch
    rtl_433 if it dies").

    ``factory`` builds a fresh line iterator (e.g. ``lambda: stream_live(dongle)``). When the
    iterator is exhausted or raises (rtl_433 exited / crashed), the stream is relaunched after
    an exponential backoff. This is per-dongle supervision — distinct from systemd
    Restart=always, which would restart the whole process and drop every other dongle's stream.

    Purely orchestration: no hardware, no rtl_433 binary — a fixture/mock factory drives it in
    tests. ``max_relaunches=None`` supervises forever (production); a finite value bounds it so
    tests terminate. ``sleep`` is injectable so tests don't actually wait.
    """
    relaunches = 0
    delay = backoff_s
    while True:
        produced = False
        try:
            for line in factory():
                produced = True
                delay = backoff_s  # a healthy stream resets the backoff
                yield line
        except Exception as exc:  # a crashed producer is just another "died" case (Pi §9)
            log.warning("SC event=stream_error err=%s", exc)
        if max_relaunches is not None and relaunches >= max_relaunches:
            return
        relaunches += 1
        wait = delay
        log.warning(
            "SC event=stream_relaunch attempt=%d backoff_s=%.1f produced=%s",
            relaunches, wait, produced,
        )
        if on_relaunch is not None:
            on_relaunch(relaunches, wait)
        sleep(wait)
        delay = min(delay * 2, backoff_max_s)


def build_argv(
    dongle: DongleConfig, extra: list[str] | None = None, all_protocols: bool = False
) -> list[str]:
    """Baseline decode stream (Pi §4). Multi-freq hop when >1 freq configured.

    Matches the §4 baseline: -M time:iso:tz -M level -M protocol -M stats (periodic health)
    plus -Y autolevel (adaptive detection level).

    `all_protocols` adds ``-G 4`` — enable EVERY rtl_433 decoder, not just the default set. That
    makes rtl_433 emit *every* protocol that matches a burst (multi-candidate decode, System §6),
    so the same signal can surface several candidate fingerprints. It is noisier (flaky decoders
    fire on RF hash); the confidence gate (`plausibility.py`) is what keeps that honest.
    """
    argv = [
        "rtl_433", "-F", "json",
        "-M", "time:iso:tz", "-M", "level", "-M", "protocol", "-M", "stats",
        # -M bits keeps the RAW demodulated payload on every row, even for decoded frames. The
        # decode is only a *guess* at the right fingerprint; retaining the bits means a wrong guess
        # stays recoverable (re-map fields / re-interpret later, System §7b) instead of thrown away.
        "-M", "bits",
    ]
    if all_protocols:
        argv += ["-G", "4"]  # enable all decoders -> all candidate matches per burst
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
    argv += ["-Y", "autolevel"]
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


def stream_live(
    dongle: DongleConfig, all_protocols: bool = False
) -> Iterator[str]:  # pragma: no cover - needs hardware
    """Spawn rtl_433 on a real dongle and stream JSON. TODO(hw): needs a dongle."""
    if not rtl433_available():
        raise RuntimeError("rtl_433 not installed")
    proc = subprocess.Popen(
        build_argv(dongle, all_protocols=all_protocols), stdout=subprocess.PIPE, text=True
    )
    assert proc.stdout is not None
    try:
        for line in proc.stdout:
            if line.strip():
                yield line
    finally:
        proc.terminate()
