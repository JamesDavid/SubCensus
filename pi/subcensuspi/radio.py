"""Single web-controlled radio owner (Pi §3, §7, §9).

One RTL-SDR, one owner. `rtl_433` (decode) and `rtl_power` (live spectrum) both claim the
USB device exclusively — they can NOT run at the same time on one dongle. Instead of two
systemd services fighting over the radio, the dashboard owns it through this one manager and
switches it between three mutually-exclusive modes:

    off       — radio idle, nothing claims the dongle (free for `rtl_test`, another tool, etc.)
    decode    — the collector runs (rtl_433 -> SQLite catalog + MQTT/HA). The 24/7 census mode.
    spectrum  — continuous live `rtl_power` sweep over an ISM band, drawn as a live waterfall.

`decode` reuses the existing collector CLI verbatim as a managed subprocess
(`python -m subcensuspi.collector.main --config …`) so supervise_stream / multi-dongle /
MQTT / capture-unknowns all behave exactly as before — we just own its lifecycle. `spectrum`
delegates to the in-process LiveSweeper. Switching modes always stops the other first, so the
one-radio invariant holds by construction.

The selected mode is persisted so a headless Pi resumes it on boot (systemd starts the web
app; the app re-applies the last mode). RX-only throughout — neither mode transmits.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import threading
from collections import deque
from pathlib import Path

MODES = ("off", "decode", "spectrum")


class RadioManager:
    """Owns the dongle; exposes off/decode/spectrum with strict mutual exclusion."""

    def __init__(self, live, config_path: str | None = None, state_path: str | None = None):
        self._lock = threading.RLock()
        self.live = live  # a LiveSweeper (spectrum mode)
        self.config_path = config_path  # collector config.yaml for decode mode
        self.state_path = state_path  # JSON file remembering the last mode (for boot resume)
        self._mode = "off"
        self._band = "ism"
        self._proc: subprocess.Popen | None = None  # decode subprocess
        self._tail: deque = deque(maxlen=25)  # last decode stdout/stderr lines (for status)
        self._tail_thread: threading.Thread | None = None
        self._last_error: str | None = None
        self._mode_before_spectrum: str = "off"  # what to resume when a spectrum look ends

    # --- capability probes ---

    def decode_available(self) -> bool:
        """Decode needs rtl_433 on PATH and a collector config to point it at."""
        return shutil.which("rtl_433") is not None and bool(self.config_path) \
            and Path(self.config_path).is_file()

    def spectrum_available(self) -> bool:
        return self.live.available()

    # --- mode control ---

    def set_mode(self, mode: str, band: str | None = None) -> dict:
        """Switch the radio to `mode` (off|decode|spectrum). Stops whatever was running first,
        so only one thing ever claims the dongle. Returns status(). Raises ValueError on a bad
        mode, FileNotFoundError / RuntimeError when the requested mode can't start."""
        if mode not in MODES:
            raise ValueError(f"mode must be one of {MODES}")
        with self._lock:
            # A spectrum look is usually a detour from the 24/7 census — remember what was
            # running so stopping the waterfall can resume it instead of leaving the radio off.
            if mode == "spectrum" and self._mode != "spectrum":
                self._mode_before_spectrum = self._mode
            elif mode != "spectrum":
                self._mode_before_spectrum = "off"
            # Always tear the radio down to idle before bringing the new mode up.
            self._stop_decode()
            self.live.stop()
            self._last_error = None
            if band:
                self._band = band
            if mode == "off":
                self._mode = "off"
            elif mode == "spectrum":
                # LiveSweeper.start() itself calls stop() first; raises FileNotFoundError if
                # rtl_power is missing (surfaced to the caller as a 503).
                self.live.start(self._band)
                self._mode = "spectrum"
            elif mode == "decode":
                self._start_decode()
                self._mode = "decode"
            self._persist()
            return self.status()

    def _start_decode(self) -> None:
        if not self.config_path or not Path(self.config_path).is_file():
            raise RuntimeError(
                "decode needs a collector config — set SUBCENSUSPI_CONFIG to your config.yaml "
                "(pi/install.sh writes one to /var/lib/subcensuspi/config.yaml)."
            )
        if shutil.which("rtl_433") is None:
            raise RuntimeError(
                "rtl_433 not installed — run pi/install.sh (installs rtl-433) before decoding."
            )
        self._tail.clear()
        argv = [sys.executable, "-m", "subcensuspi.collector.main", "--config", self.config_path]
        # Line-buffered combined output so the dashboard can show why decode stopped if it dies.
        self._proc = subprocess.Popen(
            argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self._tail_thread = threading.Thread(target=self._drain_tail, args=(self._proc,), daemon=True)
        self._tail_thread.start()

    def _drain_tail(self, proc: subprocess.Popen) -> None:
        try:
            assert proc.stdout is not None
            for line in proc.stdout:
                line = line.rstrip("\n")
                if line:
                    self._tail.append(line)
        except Exception:  # pragma: no cover - teardown race
            pass

    def _stop_decode(self) -> None:
        proc, self._proc = self._proc, None
        if proc is not None:
            try:
                proc.terminate()
                proc.wait(timeout=5)
            except Exception:  # pragma: no cover
                try:
                    proc.kill()
                except Exception:
                    pass

    def after_spectrum_mode(self) -> str:
        """The mode to return to when a spectrum look ends (Pi §3): whatever ran before the sweep
        — so stopping the waterfall resumes the 24/7 census rather than silently leaving the
        radio (persistently) off."""
        m = self._mode_before_spectrum
        return m if m in ("off", "decode") else "off"

    # --- introspection ---

    def _decode_alive(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    def status(self) -> dict:
        """Current radio state for the dashboard. If a mode died on the radio (e.g. dongle busy /
        absent), the reported mode reflects reality and `error` carries the reason."""
        with self._lock:
            mode = self._mode
            error = self._last_error
            decode_running = False
            if mode == "decode":
                if self._decode_alive():
                    decode_running = True
                else:  # subprocess exited on its own — decode is effectively off
                    rc = self._proc.poll() if self._proc is not None else None
                    tail = " | ".join(list(self._tail)[-3:]) or "collector exited"
                    error = f"decode stopped (rc={rc}): {tail}"
                    mode = "off"
                    self._mode = "off"
            spectrum = self.live.snapshot()
            spectrum_running = bool(spectrum.get("running"))
            if mode == "spectrum" and not spectrum_running:
                # LiveSweeper captured the exit reason (busy dongle, etc.).
                error = spectrum.get("error") or "spectrum stopped"
                mode = "off"
                self._mode = "off"
            return {
                "mode": mode,
                "band": self._band,
                "error": error,
                "decode": {
                    "running": decode_running,
                    "available": self.decode_available(),
                    "tail": list(self._tail)[-5:],
                },
                "spectrum": {
                    "running": spectrum_running,
                    "available": self.spectrum_available(),
                    "range": spectrum.get("range"),
                },
                "modes": list(MODES),
            }

    # --- persistence (boot resume) ---

    def _persist(self) -> None:
        if not self.state_path:
            return
        try:
            p = Path(self.state_path)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(json.dumps({"mode": self._mode, "band": self._band}), encoding="utf-8")
        except OSError:  # pragma: no cover - best-effort; a read-only FS just loses resume
            pass

    def load_persisted(self) -> dict | None:
        if not self.state_path:
            return None
        try:
            return json.loads(Path(self.state_path).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None

    def resume(self) -> None:
        """Re-apply the persisted mode at startup (headless boot). Best-effort: if the radio can't
        start the saved mode (no dongle yet, rtl tools missing), record the error and stay off
        rather than crash the web app — the user can flip the mode from the dashboard later."""
        saved = self.load_persisted()
        if not saved:
            return
        mode = saved.get("mode", "off")
        band = saved.get("band", "ism")
        if mode not in MODES or mode == "off":
            self._band = band or "ism"
            return
        try:
            self.set_mode(mode, band=band)
        except Exception as exc:  # dongle absent / rtl tools missing at boot — stay off, note why
            with self._lock:
                self._mode = "off"
                self._last_error = f"could not resume {mode} at startup: {exc}"
