#!/usr/bin/env python3
"""Build the merged flash image the web flasher serves.

Runs `pio run` + `buildfs`, then merges bootloader + partitions + boot_app0 + app + LittleFS
(the web UI) into esp/flasher/firmware/subcensusesp.bin — a single image flashed at offset 0.
Used by CI (.github/workflows/deploy-flasher.yml) AND locally, so the flasher and the source
never drift.

Offsets (esp32dev default OTA partition table):
  0x1000 bootloader · 0x8000 partitions · 0xe000 boot_app0 · 0x10000 app · 0x290000 littlefs

  python esp/flasher/build_merged.py            # build firmware + fs + merge
  python esp/flasher/build_merged.py --no-build # merge only (reuse existing .pio build)
"""

from __future__ import annotations

import glob
import subprocess
import sys
from pathlib import Path

ESP = Path(__file__).resolve().parents[1]
BUILD = ESP / ".pio" / "build" / "esp32dev"
OUT = ESP / "flasher" / "firmware"
PIO_HOME = Path.home() / ".platformio"


def sh(*args: str, cwd: Path | None = None) -> None:
    print("+ " + " ".join(str(a) for a in args))
    subprocess.run([str(a) for a in args], check=True, cwd=str(cwd) if cwd else None)


def find_one(pattern: str, what: str) -> str:
    hits = sorted(glob.glob(pattern, recursive=True))
    if not hits:
        raise FileNotFoundError(f"could not find {what} ({pattern})")
    return hits[0]


def main(argv: list[str]) -> int:
    if "--no-build" not in argv:
        sh(sys.executable, "-m", "platformio", "run", "-e", "esp32dev", cwd=ESP)
        sh(sys.executable, "-m", "platformio", "run", "-e", "esp32dev", "-t", "buildfs", cwd=ESP)

    boot_app0 = find_one(str(PIO_HOME / "packages" / "framework-arduinoespressif32*"
                             / "tools" / "partitions" / "boot_app0.bin"), "boot_app0.bin")
    esptool = find_one(str(PIO_HOME / "packages" / "tool-esptoolpy*" / "esptool.py"), "esptool.py")

    OUT.mkdir(parents=True, exist_ok=True)
    merged = OUT / "subcensusesp.bin"
    sh(
        sys.executable, esptool, "--chip", "esp32", "merge_bin", "-o", str(merged),
        "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB",
        "0x1000", str(BUILD / "bootloader.bin"),
        "0x8000", str(BUILD / "partitions.bin"),
        "0xe000", boot_app0,
        "0x10000", str(BUILD / "firmware.bin"),
        "0x290000", str(BUILD / "littlefs.bin"),
    )
    size = merged.stat().st_size
    print(f"\nwrote {merged.relative_to(ESP.parent)} ({size} bytes) — flash at offset 0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
