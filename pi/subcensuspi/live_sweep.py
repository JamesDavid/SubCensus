"""Continuous live spectrum sweep (Pi §7) — a background rtl_power that streams a sweep every
~1 s over a narrow ISM band, into an in-memory ring the dashboard polls to draw a LIVE waterfall.

This is the "sit on the active bands and watch them continuously" mode (vs. the one-shot recon
pass that writes occupancy.csv). Narrow range + fine bins + fast revisit = actually catches short
periodic ISM bursts. RX-only. Uses the single dongle exclusively while running — the collector
(decode) can't run at the same time (one radio).

rtl_power streams CSV to stdout (`-` filename); each timestamp is one full sweep (possibly several
tuner-hop lines). We group by timestamp and keep the most recent N sweeps.
"""

from __future__ import annotations

import shutil
import subprocess
import threading
from collections import deque

from .occupancy_pass import (
    SPECTRUM_BUCKETS,
    _bucket_freqs,
    bucket_sweep,
    parse_rtl_power_line,
    resolve_range,
)


class LiveSweeper:
    """Manages a streaming rtl_power subprocess + a ring of recent sweeps (thread-safe)."""

    def __init__(self, max_sweeps: int = 90):
        self._lock = threading.Lock()
        self._proc: subprocess.Popen | None = None
        self._thread: threading.Thread | None = None
        self._running = False
        self._range: str | None = None
        self._sweeps: deque = deque(maxlen=max_sweeps)  # (ts, {freq_hz: dbm})
        self._fmin: int | None = None
        self._fmax: int | None = None
        self._error: str | None = None

    # --- lifecycle ---

    def available(self) -> bool:
        return shutil.which("rtl_power") is not None

    def start(self, band: str | None = None, integration_s: int = 1) -> str:
        """(Re)start streaming over `band` (a preset name or a raw low:high:bin). Returns the
        resolved range. Raises FileNotFoundError if rtl_power is missing."""
        if not self.available():
            raise FileNotFoundError("rtl_power not found — install rtl-sdr (sudo apt install rtl-sdr)")
        self.stop()
        rng = resolve_range(band)
        with self._lock:
            self._sweeps.clear()
            self._range = rng
            self._fmin = self._fmax = None
            self._error = None
        # stream to stdout; -i sets the per-line integration (≈ sweep cadence), no -e => forever
        argv = ["rtl_power", "-f", rng, "-i", str(integration_s), "-"]
        self._proc = subprocess.Popen(
            argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1
        )
        self._running = True
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()
        return rng

    def _reader(self) -> None:
        cur_ts: str | None = None
        cur: dict[int, float] = {}
        try:
            assert self._proc is not None and self._proc.stdout is not None
            for line in self._proc.stdout:
                parsed = parse_rtl_power_line(line)
                if parsed is None:
                    continue
                ts, _band_low, pairs = parsed
                if cur_ts is not None and ts != cur_ts:
                    self._push(cur_ts, cur)
                    cur = {}
                cur_ts = ts
                for f, d in pairs:
                    cur[f] = d
        except Exception:  # pragma: no cover - stream teardown races
            pass
        if cur and cur_ts is not None:
            self._push(cur_ts, cur)
        # capture why it stopped (busy dongle, etc.) for the UI
        proc = self._proc
        if proc is not None and proc.poll() not in (None, 0):
            try:
                err = (proc.stderr.read() if proc.stderr else "") or ""
            except Exception:  # pragma: no cover
                err = ""
            with self._lock:
                self._error = err.strip().splitlines()[-1] if err.strip() else "rtl_power exited"
        self._running = False

    def _push(self, ts: str, sweep: dict[int, float]) -> None:
        with self._lock:
            for f in sweep:
                if self._fmin is None or f < self._fmin:
                    self._fmin = f
                if self._fmax is None or f > self._fmax:
                    self._fmax = f
            self._sweeps.append((ts, dict(sweep)))

    def stop(self) -> None:
        proc, self._proc = self._proc, None
        self._running = False
        if proc is not None:
            try:
                proc.terminate()
                proc.wait(timeout=3)
            except Exception:  # pragma: no cover
                try:
                    proc.kill()
                except Exception:
                    pass

    # --- read ---

    def snapshot(self, buckets: int = SPECTRUM_BUCKETS) -> dict:
        """Current live state for the dashboard: {running, range, error, freqs, sweeps[]}."""
        with self._lock:
            base = {"running": self._running, "range": self._range, "error": self._error}
            if not self._sweeps or self._fmin is None or self._fmax is None or self._fmax <= self._fmin:
                return {**base, "freqs": [], "sweeps": []}
            bf = _bucket_freqs(self._fmin, self._fmax, buckets)
            rows = [{"ts": ts, "dbm": bucket_sweep(s, bf)} for ts, s in list(self._sweeps)]
            return {**base, "freqs": bf, "sweeps": rows}
