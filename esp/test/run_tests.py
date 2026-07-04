#!/usr/bin/env python3
"""Native test runner for the ESP hardware-independent logic (Debug §3.3, §6).

Compiles each esp/test/test_*.c together with the esp_*.c logic modules AND shared/core/*.c
using `zig cc` (clang) and runs it — no ESP32, no radio (the RF boundary). Reuses the same
harness (`test/core/sc_test.h`) as the shared-core suite. `pip install ziglang` provides clang.

  python esp/test/run_tests.py
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ESP = HERE.parent
REPO = ESP.parent
SRC = ESP / "src"
CORE = REPO / "shared" / "core"
CORE_TEST = REPO / "test" / "core"  # for sc_test.h
BUILD = HERE / "build"

CFLAGS = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]


def zig_cc() -> list[str]:
    return [sys.executable, "-m", "ziglang", "cc"]


def logic_sources() -> list[Path]:
    # the pure-C ESP logic (NOT subcensus_core.c — that unity-includes shared/core, which we
    # compile individually here — and NOT main.cpp, which is Arduino firmware).
    return sorted(SRC.glob("esp_*.c"))


def core_sources() -> list[Path]:
    return sorted(CORE.glob("*.c"))


def compile_and_run(test_c: Path) -> bool:
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / (test_c.stem + (".exe" if sys.platform == "win32" else ""))
    cmd = [
        *zig_cc(), *CFLAGS,
        f"-I{SRC}", f"-I{CORE}", f"-I{CORE_TEST}",
        str(test_c), *[str(s) for s in logic_sources()], *[str(s) for s in core_sources()],
        "-lm", "-o", str(exe),
    ]
    print(f"[build] {test_c.name}")
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.returncode != 0:
        print(f"[BUILD FAILED] {test_c.name}\n{build.stdout}\n{build.stderr}")
        return False
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    if run.stderr.strip():
        print(run.stderr, end="")
    ok = run.returncode == 0
    print(f"[{'PASS' if ok else 'FAIL'}]  {test_c.name}\n")
    return ok


def main(argv: list[str]) -> int:
    tests = sorted(HERE.glob("test_*.c"))
    if argv:
        tests = [t for t in tests if t.stem in set(argv)]
    if not tests:
        print("no test_*.c found")
        return 2
    results = {t.name: compile_and_run(t) for t in tests}
    passed = sum(results.values())
    print("=" * 48)
    for name, ok in results.items():
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
    print(f"  {passed}/{len(results)} test files passed")
    print("=" * 48)
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
