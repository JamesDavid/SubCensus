#!/usr/bin/env python3
"""One-command off-device validation loop per target (Debug §6).

The Pi has a networked, always-on service, so its autonomous loop is edit -> deploy over HTTP ->
observe on real hardware. The Zero (USB handheld, no network) and the ESP (networked only once
flashed) can't be driven that way from here, so their loop is the honest RF-boundary one: prove
the *processing* off-device (unit tests + compile-check + web-contract mock) and leave live radio
as the on-device step. This runs whichever of those are available on the current machine and
reports a single pass/fail, so a human OR an agent loop can invoke one command:

    python tools/check.py all        # everything runnable here
    python tools/check.py esp        # ESP: native tests + pio compile + web mock
    python tools/check.py zero       # Zero: shared-core logic tests + ufbt compile (if installed)
    python tools/check.py pi shared  # multiple targets

Missing toolchains (ufbt, platformio) are reported as SKIP with the reason, not FAIL — so the loop
stays green on what it can actually check and tells you what needs a fuller environment.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PY = sys.executable


def module_available(mod: str) -> bool:
    """True if `python -m <mod> --version` runs — the reliable check for pip-installed tools
    (ufbt, platformio) whose bare command may not be on PATH."""
    try:
        r = subprocess.run([PY, "-m", mod, "--version"], capture_output=True, timeout=60)
        return r.returncode == 0 or bool(r.stdout)  # ufbt prints SCons banner w/ nonzero sometimes
    except (OSError, subprocess.SubprocessError):
        return False


class Step:
    def __init__(self, name: str, argv: list[str], cwd: Path, need_mod: str | None = None):
        self.name, self.argv, self.cwd, self.need_mod = name, argv, cwd, need_mod

    def run(self) -> str:
        if self.need_mod and not module_available(self.need_mod):
            print(f"  SKIP  {self.name}  (needs 'python -m {self.need_mod}' — not installed here)")
            return "skip"
        print(f"  ---- {self.name}: {' '.join(self.argv)}")
        r = subprocess.run(self.argv, cwd=str(self.cwd))
        ok = r.returncode == 0
        print(f"  {'PASS' if ok else 'FAIL'}  {self.name}")
        return "pass" if ok else "fail"


def steps_for(target: str) -> list[Step]:
    esp, zero, pi = REPO / "esp", REPO / "zero", REPO / "pi"
    if target == "shared":
        return [Step("shared/core native tests", [PY, "test/core/run_tests.py"], REPO)]
    if target == "esp":
        return [
            Step("esp native tests", [PY, "esp/test/run_tests.py"], REPO),
            Step("esp web-mock contract", [PY, "-m", "pytest", "tools/test_esp_web.py", "-q"], esp),
            Step("esp pio compile-check", [PY, "-m", "platformio", "run", "-e", "esp32dev"], esp,
                 need_mod="platformio"),
        ]
    if target == "zero":
        # the Zero's testable logic IS shared/core (furi-dependent zero/*.c is on-device only);
        # ufbt compile-check validates the FAP itself when the toolchain is present.
        return [
            Step("shared/core native tests (Zero logic core)", [PY, "test/core/run_tests.py"], REPO),
            Step("zero ufbt compile-check", [PY, "-m", "ufbt"], zero, need_mod="ufbt"),
        ]
    if target == "pi":
        return [Step("pi pytest", [PY, "-m", "pytest", "-q"], pi)]
    raise SystemExit(f"unknown target: {target} (use pi|esp|zero|shared|all)")


def main(argv: list[str]) -> int:
    targets = argv or ["all"]
    if "all" in targets:
        targets = ["shared", "pi", "esp", "zero"]
    results: dict[str, str] = {}
    for t in targets:
        print(f"\n=== {t} ===")
        for step in steps_for(t):
            results[f"{t}: {step.name}"] = step.run()
    print("\n" + "=" * 60)
    fails = [k for k, v in results.items() if v == "fail"]
    skips = [k for k, v in results.items() if v == "skip"]
    for k, v in results.items():
        print(f"  {v.upper():4}  {k}")
    print(f"\n  {sum(v == 'pass' for v in results.values())} passed, "
          f"{len(fails)} failed, {len(skips)} skipped")
    if skips:
        print("  (skips need a fuller environment — ufbt for the Flipper FAP, platformio for ESP)")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
