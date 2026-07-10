"""Re-decode a captured burst against every decoder (System §6 multi-candidate).

The collector saves each detected transmission as a raw `.cu8` sample (rtl_433 -S all). This
replays one sample file through rtl_433 — no dongle needed, so it works while live decode owns
the radio — and returns every protocol that claims the burst, ranked by the §6 confidence gate.

This is the honest mechanism for "which decoder is right?": keep the received bits, then test
them against all fingerprints on demand. It first tries the full decoder set (``-G 4``, which
enables even default-disabled decoders on rtl_433 builds that support it — the flag is
undocumented on some packaged versions, so a rejection is caught here and we fall back to the
default ~200-decoder set instead of failing). Candidates already present in the catalog are
marked, so the list reads as "matches these N things — one of them yours?".
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path
from typing import Callable

from .db import Database, device_id_for
from .plausibility import assess
from .readings import humanize_reading


def rtl433_available() -> bool:
    return shutil.which("rtl_433") is not None


def _run(path: str, timeout_s: int = 60) -> list[str]:  # pragma: no cover - needs rtl_433
    """Replay `path` through rtl_433, full decoder set if supported, default set otherwise."""
    base = ["rtl_433", "-r", path, "-F", "json", "-M", "protocol"]
    for argv in (base + ["-G", "4"], base):  # -G 4 first; fall back if this build rejects it
        try:
            proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout_s)
        except FileNotFoundError:
            raise RuntimeError("rtl_433 not installed")
        lines = [l for l in proc.stdout.splitlines() if l.strip()]
        if proc.returncode == 0 or lines:
            return lines
    return []


def redecode_file(
    path: str | Path, db: Database | None = None, run: Callable[[str], list[str]] = _run
) -> list[dict]:
    """All candidate decodes of one captured burst, best first. Each candidate: model/id/channel,
    frames (intra-burst repeats), humanized reading, in_catalog (already a known device?), and the
    §6 confidence + reasons. `run` is injectable so tests need no rtl_433 binary."""
    groups: dict[tuple, dict] = {}
    for line in run(str(path)):
        try:
            obj = json.loads(line)
        except (ValueError, TypeError):
            continue
        if not isinstance(obj, dict) or "model" not in obj:
            continue
        # same normalization as the live parser, so catalog lookups line up
        key = (str(obj.get("model")), str(obj.get("id", obj.get("address", ""))),
               str(obj.get("channel", "")))
        g = groups.setdefault(
            key, {"model": key[0], "dev_id": key[1], "channel": key[2],
                  "frames": 0, "raw_json": line},
        )
        g["frames"] += 1
    out = []
    for g in groups.values():
        count = 1
        in_catalog = False
        if db is not None:
            row = db.get_device(device_id_for(g["model"], g["dev_id"], g["channel"]))
            if row is not None:
                in_catalog = True
                count = row["count"] or 1  # catalog corroboration feeds the confidence score
        a = assess(g["raw_json"], model=g["model"], count=count)
        out.append({
            "model": g["model"], "dev_id": g["dev_id"], "channel": g["channel"],
            "frames": g["frames"],
            "reading": humanize_reading(g["raw_json"]),
            "in_catalog": in_catalog,
            "confidence": round(a.confidence, 2),
            "plausible": a.plausible,
            "reasons": a.reasons,
        })
    out.sort(key=lambda c: c["confidence"], reverse=True)
    return out
