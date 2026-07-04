#!/usr/bin/env python3
"""Native test runner for shared/core (Debug §1.1, §6 step 1).

Compiles each test/core/test_*.c together with shared/core/*.c using `zig cc` (clang)
and runs it. A non-zero exit from any test executable = failure. No hardware, no radio
— fixtures + logic only (the RF boundary, CLAUDE.md / Debug §7).

`zig cc` is used as a hermetic, pip-installable clang so the suite runs on any machine
with Python (`pip install ziglang`) — no system gcc/clang/make required.

Usage:
  python test/core/run_tests.py            # build + run all core tests
  python test/core/run_tests.py test_crc   # run a single test by stem
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
CORE = REPO / "shared" / "core"
FIXTURES = REPO / "test" / "fixtures"
BUILD = HERE / "build"

CFLAGS = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]


def _fixtures_define() -> str:
    # Forward-slash path so it embeds cleanly as a C string literal on Windows too.
    return '-DSC_FIXTURES_DIR="' + FIXTURES.as_posix() + '"'


def zig_cc() -> list[str]:
    return [sys.executable, "-m", "ziglang", "cc"]


def core_sources() -> list[Path]:
    return sorted(CORE.glob("*.c"))


def compile_and_run(test_c: Path) -> bool:
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / (test_c.stem + (".exe" if sys.platform == "win32" else ""))
    cmd = [
        *zig_cc(),
        *CFLAGS,
        _fixtures_define(),
        f"-I{CORE}",
        f"-I{HERE}",
        str(test_c),
        *[str(s) for s in core_sources()],
        "-lm",
        "-o",
        str(exe),
    ]
    print(f"[build] {test_c.name}")
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.returncode != 0:
        print(f"[BUILD FAILED] {test_c.name}")
        print(build.stdout)
        print(build.stderr)
        return False
    if build.stderr.strip():
        # warnings (shouldn't happen with -Werror, but surface anything)
        print(build.stderr)
    print(f"[run]   {test_c.name}")
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
        wanted = set(argv)
        tests = [t for t in tests if t.stem in wanted]
        if not tests:
            print(f"no matching tests for {argv}")
            return 2
    if not tests:
        print("no test_*.c found")
        return 2
    results = {t.name: compile_and_run(t) for t in tests}
    passed = sum(results.values())
    total = len(results)
    print("=" * 48)
    for name, ok in results.items():
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
    print(f"  {passed}/{total} test files passed")
    print("=" * 48)
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
