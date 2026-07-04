"""Passive field-map discovery (System §7b) over the Pi's events corpus.

The Pi is the STRONGEST at the passive layers: its continuous corpus of every-reception rows
gives the cleanest differential bitfield analysis + checksum search, and HA/MQTT values are
co-located for ground-truth correlation. RX-only, so NO active confirmation (that's Zero/Esp).

Three layers (System §7b), cheapest/safest first — all passive, no TX:
  1. differential bitfield analysis  -> segment bits into static / slow-varying / counter
  2. checksum discovery              -> name the trailing check byte's algorithm
  3. ground-truth correlation        -> regress a slow field against a known value (e.g. temp)

Output is a PROPOSED field_maps/ entry — surfaced for user confirmation, NEVER auto-committed
(System §7b). The heavy crunch can also run in build_signatures.py; this is the on-Pi analysis.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field

from .db import Database
from .dsp import crc, diff


@dataclass
class FieldSegment:
    name: str
    start_bit: int
    length: int
    cls: str  # static | slow | counter | checksum
    semantics: str | None = None
    correlation: float | None = None


@dataclass
class FieldMapProposal:
    signature: str
    device_id: str
    model: str
    n_frames: int
    n_bytes: int
    fields: list[FieldSegment] = field(default_factory=list)
    checksum: dict | None = None
    confidence: float = 0.0
    reasoning: str = ""


def frames_from_events(db: Database, device_id: str, hex_field: str = "data") -> list[bytes]:
    """Reconstruct raw byte frames from a device's events (raw_json[hex_field])."""
    frames: list[bytes] = []
    for ev in db.device_events(device_id):
        try:
            obj = json.loads(ev["raw_json"])
        except (TypeError, json.JSONDecodeError):
            continue
        hexstr = obj.get(hex_field)
        if isinstance(hexstr, str):
            try:
                frames.append(bytes.fromhex(hexstr))
            except ValueError:
                continue
    return frames


def ground_truth_series(db: Database, device_id: str, gt_field: str) -> list[float | None]:
    out: list[float | None] = []
    for ev in db.device_events(device_id):
        try:
            obj = json.loads(ev["raw_json"])
            v = obj.get(gt_field)
            out.append(float(v) if v is not None else None)
        except (TypeError, ValueError, json.JSONDecodeError):
            out.append(None)
    return out


def _pearson(a: list[float], b: list[float]) -> float:
    n = len(a)
    if n < 2:
        return 0.0
    ma, mb = sum(a) / n, sum(b) / n
    num = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    da = math.sqrt(sum((x - ma) ** 2 for x in a))
    db_ = math.sqrt(sum((x - mb) ** 2 for x in b))
    if da == 0 or db_ == 0:
        return 0.0
    return num / (da * db_)


def discover_checksum(frames: list[bytes]) -> crc.ChecksumSpec | None:
    """If the trailing byte is a checksum of the preceding bytes consistently across the
    whole corpus, return the named spec (System §7b layer 2)."""
    if len(frames) < 2 or len(frames[0]) < 2:
        return None
    spec = crc.checksum_search(frames[0][:-1], frames[0][-1])
    if spec is None:
        return None
    for f in frames:
        if len(f) != len(frames[0]):
            return None
        if crc.compute(spec, f[:-1]) != f[-1]:
            return None
    return spec


def analyze_device(
    db: Database, device_id: str, *, hex_field: str = "data", ground_truth_field: str | None = None
) -> FieldMapProposal | None:
    frames = frames_from_events(db, device_id, hex_field)
    frames = [f for f in frames if len(f) == len(frames[0])] if frames else []
    dev = db.get_device(device_id)
    model = dev["model"] if dev else ""
    if len(frames) < 2:
        return None

    nbytes = len(frames[0])
    prof = diff.analyze(frames, nbytes * 8)

    segments: list[FieldSegment] = []
    for b in range(nbytes):
        bits = prof[b * 8 : (b + 1) * 8]
        if all(p.cls == diff.STATIC for p in bits):
            cls = "static"
        elif any(p.cls == diff.COUNTER for p in bits):
            cls = "counter"
        else:
            cls = "slow"
        segments.append(FieldSegment(f"byte{b}", b * 8, 8, cls))

    checksum = discover_checksum(frames)
    checksum_dict = None
    if checksum is not None:
        segments[-1].cls = "checksum"
        checksum_dict = {"kind": checksum.kind.value, "poly": checksum.poly,
                         "init": checksum.init, "over_bytes": nbytes - 1}

    # ground-truth correlation for slow segments (System §7b layer 3)
    correlated = 0
    if ground_truth_field:
        gt = ground_truth_series(db, device_id, ground_truth_field)
        if all(v is not None for v in gt):
            for seg in segments:
                if seg.cls != "slow":
                    continue
                byte_idx = seg.start_bit // 8
                vals = [float(f[byte_idx]) for f in frames]
                r = _pearson(vals, [float(v) for v in gt])
                if abs(r) > 0.95:
                    seg.semantics = f"tracks {ground_truth_field}"
                    seg.correlation = round(r, 3)
                    correlated += 1

    # confidence: more frames + a named checksum + a correlated field => higher
    conf = min(1.0, 0.3 + 0.05 * len(frames) + (0.2 if checksum else 0) + 0.15 * correlated)
    reasoning = (
        f"{len(frames)} frames; "
        f"{sum(1 for s in segments if s.cls == 'static')} static, "
        f"{sum(1 for s in segments if s.cls == 'counter')} counter, "
        f"{sum(1 for s in segments if s.cls == 'slow')} slow segment(s); "
        f"checksum={checksum.kind.value if checksum else 'none'}. "
        "PROPOSAL — passive (no TX); user confirms before writing field_maps/."
    )
    return FieldMapProposal(
        signature=f"{model}:{device_id}", device_id=device_id, model=model,
        n_frames=len(frames), n_bytes=nbytes, fields=segments, checksum=checksum_dict,
        confidence=round(conf, 3), reasoning=reasoning,
    )


def proposal_to_dict(p: FieldMapProposal) -> dict:
    return {
        "signature": p.signature, "device_id": p.device_id, "model": p.model,
        "n_frames": p.n_frames, "n_bytes": p.n_bytes,
        "fields": [
            {"name": s.name, "start_bit": s.start_bit, "length": s.length,
             "class": s.cls, "semantics": s.semantics, "correlation": s.correlation}
            for s in p.fields
        ],
        "checksum": p.checksum, "confidence": p.confidence, "reasoning": p.reasoning,
    }
