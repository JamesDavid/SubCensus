"""The classification brain, wired into live decode (System §6).

System §6 step 1: a known protocol decode is looked up in `protocol_map.csv` (rtl_433 model ->
friendly name + device_class) and proposed with `match_source=decoder`, high confidence. This is
the live path on the Pi (its rtl_433 decode set is why the Pi is the strong protocol_map seed).
k-NN against `fingerprints.csv` (step 2) is for RAW/undecoded captures — not the live decoded
path — so it stays in `dsp/` for the unknowns pipeline.

Active-learning loop (System §6): when the user confirms a label/class in the dashboard, upsert
that model into the brain so every future decode of it is classified immediately. The runtime
brain lives in `signatures_dir` (the global brain shared with the Zero); it is seeded from the
packaged `shared/signatures/` and grows with use. **Never auto-relabel** — only a user
confirmation calls `learn()`; `classify()` only ever proposes.
"""

from __future__ import annotations

import csv
import os
import threading
from pathlib import Path

PROTOCOL_MAP = "protocol_map.csv"
_PM_HEADER = ["protocol", "friendly_name", "device_class", "typical_use", "notes"]
# packaged seed shipped in the repo: pi/subcensuspi/brain.py -> repo root -> shared/signatures
_SEED_DIR = Path(__file__).resolve().parents[2] / "shared" / "signatures"

# A decoder match is a known-protocol identification (§6.1 "high confidence"). This is the
# IDENTITY axis; whether the reading is physically real is the separate plausibility axis.
DECODER_CONFIDENCE = 0.9


class Brain:
    """protocol_map lookup + active-learning upsert. Thread-safe (the collector classifies from
    its consumer thread; the web app learns from request threads; both may share one instance)."""

    def __init__(self, signatures_dir: str | Path | None = None):
        self.dir = Path(signatures_dir) if signatures_dir else None
        self._lock = threading.Lock()
        self._map: dict[str, dict] = {}
        self.load()

    def _runtime_path(self) -> Path | None:
        return (self.dir / PROTOCOL_MAP) if self.dir else None

    def _source_path(self) -> Path | None:
        """Prefer the runtime brain (signatures_dir); fall back to the read-only packaged seed."""
        rp = self._runtime_path()
        if rp is not None and rp.is_file():
            return rp
        seed = _SEED_DIR / PROTOCOL_MAP
        return seed if seed.is_file() else None

    def load(self) -> int:
        """(Re)load protocol_map into memory. Returns the row count."""
        m: dict[str, dict] = {}
        p = self._source_path()
        if p is not None:
            try:
                with p.open(newline="", encoding="utf-8") as fh:
                    for row in csv.DictReader(fh):
                        proto = (row.get("protocol") or "").strip()
                        if proto:
                            m[proto] = row
            except OSError:  # pragma: no cover - brain unreadable -> just classify nothing
                pass
        with self._lock:
            self._map = m
        return len(m)

    def classify(self, model: str) -> dict | None:
        """Propose an identity for a decoded model (System §6.1). None if the model isn't in the
        brain yet (an unmapped-but-decoded device — a candidate for the user to label)."""
        with self._lock:
            row = self._map.get(model)
        if not row:
            return None
        return {
            "name": (row.get("friendly_name") or model).strip() or model,
            "device_class": (row.get("device_class") or "").strip(),
            "confidence": DECODER_CONFIDENCE,
            "source": "decoder",
        }

    def learn(self, protocol: str, friendly_name: str = "", device_class: str = "",
              typical_use: str = "") -> bool:
        """Upsert a user-confirmed model into the runtime brain and persist it (active-learning
        loop, §6). No-op (returns False) if no signatures_dir is configured. Atomic write."""
        if not self.dir or not protocol:
            return False
        with self._lock:
            rows = dict(self._map)
            prev = rows.get(protocol, {})
            rows[protocol] = {
                "protocol": protocol,
                "friendly_name": friendly_name or prev.get("friendly_name") or protocol,
                "device_class": device_class or prev.get("device_class") or "",
                "typical_use": typical_use or prev.get("typical_use") or "",
                "notes": "user-confirmed (Pi live)",
            }
            self.dir.mkdir(parents=True, exist_ok=True)
            path = self.dir / PROTOCOL_MAP
            tmp = path.with_suffix(".csv.tmp")
            with tmp.open("w", newline="", encoding="utf-8") as fh:
                w = csv.DictWriter(fh, fieldnames=_PM_HEADER)
                w.writeheader()
                for proto in sorted(rows):
                    r = rows[proto]
                    w.writerow({k: r.get(k, "") for k in _PM_HEADER})
            os.replace(tmp, path)
            self._map = rows
        return True

    def __len__(self) -> int:
        with self._lock:
            return len(self._map)
