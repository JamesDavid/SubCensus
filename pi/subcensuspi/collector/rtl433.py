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
    dongle: DongleConfig, extra: list[str] | None = None, samples: bool = False
) -> list[str]:
    """Baseline decode stream (Pi §4). Multi-freq hop when >1 freq configured.

    Matches the §4 baseline: -M time:iso:tz -M level -M protocol -M stats (periodic health)
    plus -Y autolevel (adaptive detection level).

    NOTE: no ``-G`` here. It is undocumented on the rtl_433 Debian/RaspiOS ships (verified against
    the bookworm man page) — an unsupported flag makes rtl_433 exit instantly and the supervisor
    relaunch it forever, silently killing the census. The default decoder set already covers ~200
    protocols; "test a burst against EVERYTHING" is done offline on captured samples
    (`redecode.py`), where a failing flag is catchable instead of fatal.

    `samples` adds ``-S all`` (documented): save every detected transmission as a raw
    `g<N>_<freq>M_<rate>k.cu8` sample in the process CWD. That is the honest "keep the received
    bits, decide the right decoder later" mechanism — any saved burst can be replayed through any
    decoder set at any time (System §6 multi-candidate), no dongle needed.
    """
    argv = [
        "rtl_433", "-F", "json",
        "-M", "time:iso:tz", "-M", "level", "-M", "protocol", "-M", "stats",
        # -M bits adds raw bit representation where a decoder exposes code outputs (documented:
        # "add bit representation to code outputs"). It does NOT cover every decoder — the .cu8
        # samples above are the reliable evidence trail; this is a cheap bonus where available.
        "-M", "bits",
    ]
    if samples:
        argv += ["-S", "all"]  # one raw .cu8 per detected transmission (re-decodable later)
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


def prune_samples(dir_path: str | Path, max_gb: float = 2.0, max_files: int = 2000) -> int:
    """Rolling window for the -S sample capture: delete oldest .cu8 until under the caps.
    rtl_433 never prunes its own sample files, so an always-on census would eventually fill the
    SD card; this keeps the newest window (the re-decodable evidence) bounded. Returns #removed."""
    p = Path(dir_path)
    if not p.is_dir():
        return 0
    files = sorted(p.glob("*.cu8"), key=lambda f: f.stat().st_mtime)  # oldest first
    sizes = {f: f.stat().st_size for f in files}
    total = sum(sizes.values())
    removed = 0
    while files and (total > max_gb * 1e9 or len(files) > max_files):
        f = files.pop(0)
        try:
            f.unlink()
            total -= sizes[f]
            removed += 1
        except OSError:  # pragma: no cover - deleted underneath us
            pass
    return removed


def stream_live(
    dongle: DongleConfig, capture_dir: str | None = None
) -> Iterator[str]:  # pragma: no cover - needs hardware
    """Spawn rtl_433 on a real dongle and stream JSON. `capture_dir` (if set) becomes the
    process CWD and enables -S all, so every detected burst lands there as a raw .cu8 sample
    (pruned to a rolling window). TODO(hw): needs a dongle."""
    if not rtl433_available():
        raise RuntimeError("rtl_433 not installed")
    cwd = None
    if capture_dir:
        Path(capture_dir).mkdir(parents=True, exist_ok=True)
        prune_samples(capture_dir)  # each (re)launch trims the window; a janitor covers long runs
        cwd = capture_dir
    proc = subprocess.Popen(
        build_argv(dongle, samples=bool(capture_dir)), stdout=subprocess.PIPE, text=True, cwd=cwd
    )
    assert proc.stdout is not None
    try:
        for line in proc.stdout:
            if line.strip():
                yield line
    finally:
        proc.terminate()
