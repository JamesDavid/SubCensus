"""Passive field-map discovery over a place's capture corpus (System §7b, §8 build_signatures).

Reconstructs per-device frame corpora from a SubCensusZero place's `.sub` captures (grouped by
frequency), runs the passive differential bitfield analysis + checksum-family search, and PROPOSES
`signatures/field_maps/<key>.fmap` entries in the shared on-disk format that the Zero/Esp editors
read (sc_fieldmap). Passive — no TX. **Proposes, never auto-commits** (System §7b): every entry is
written with `source proposed` for the user to confirm.

The differential / checksum / slicing logic mirrors shared/core (sc_diff, sc_crc, sc_slice,
sc_fieldmap) by behaviour so a tools-proposed map matches an on-device one. Kept dependency-free
(no pi import) so build_signatures stays standalone.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

# ScChecksumKind (shared/core/sc_crc.h) — keep the integer values identical for .fmap round-trip.
CK_NONE, CK_XOR, CK_SUM, CK_CRC8, CK_CRC8LE, CK_LFSR8 = 0, 1, 2, 3, 4, 5
# ScFieldClass (shared/core/sc_fieldmap.h)
CLS_STATIC, CLS_SLOW, CLS_COUNTER, CLS_CHECKSUM, CLS_DATA = 0, 1, 2, 3, 4
_CLS_STR = {CLS_STATIC: "static", CLS_SLOW: "slow", CLS_COUNTER: "counter",
            CLS_CHECKSUM: "checksum", CLS_DATA: "data"}
_DIFF_COUNTER_RATE = 0.8


@dataclass
class Field:
    name: str
    start_bit: int
    length: int
    cls: int


@dataclass
class FieldMap:
    protocol: str
    nbits: int
    modulation: int = 0
    fields: list[Field] = field(default_factory=list)
    checksum_kind: int = CK_NONE
    checksum_poly: int = 0
    checksum_init: int = 0
    checksum_over_bytes: int = 0


# --- .sub RAW parse + slice (mirror sc_sub / sc_slice) ---

def parse_sub(text: str) -> tuple[list[int], int, str]:
    """Return (signed timings, frequency_hz, preset) from a Flipper RAW .sub."""
    freq, preset, timings = 0, "", []
    for line in text.splitlines():
        if line.startswith("Frequency:"):
            freq = int(line.split(":", 1)[1].strip() or "0")
        elif line.startswith("Preset:"):
            preset = line.split(":", 1)[1].strip()
        elif line.startswith("RAW_Data:"):
            timings += [int(x) for x in re.findall(r"-?\d+", line.split(":", 1)[1])]
    return timings, freq, preset


def _unit(timings: list[int]) -> int:
    """Shortest dominant symbol duration (approx sc_feature sym_dur_us[0])."""
    mags = sorted(abs(t) for t in timings if t)
    if not mags:
        return 250
    # smallest cluster centre: median of the shortest ~30% of pulses
    head = mags[: max(1, len(mags) // 3)]
    return max(1, head[len(head) // 2])


def slice_bits(timings: list[int], unit: int, cap_bytes: int = 16) -> tuple[bytes, int]:
    """Quantize each run to round(|dur|/unit) symbol units (mirror sc_slice_bits, MSB-first)."""
    bits: list[int] = []
    maxbits = cap_bytes * 8
    for d in timings:
        if not d or len(bits) >= maxbits:
            continue
        level = 1 if d > 0 else 0
        k = max(1, round(abs(d) / unit))
        bits += [level] * k
    bits = bits[:maxbits]
    out = bytearray(cap_bytes)
    for i, b in enumerate(bits):
        if b:
            out[i // 8] |= 0x80 >> (i % 8)
    return bytes(out), len(bits)


# --- differential (mirror sc_diff) ---

def differential(frames: list[bytes], nbits: int) -> list[int]:
    n = len(frames)
    cls = []
    for b in range(nbits):
        vals = [(frames[f][b // 8] >> (7 - (b % 8))) & 1 for f in range(n)]
        distinct = len(set(vals))
        changes = sum(1 for i in range(1, n) if vals[i] != vals[i - 1])
        rate = changes / (n - 1) if n > 1 else 0.0
        if distinct == 1:
            cls.append(CLS_STATIC)
        elif rate >= _DIFF_COUNTER_RATE:
            cls.append(CLS_COUNTER)
        else:
            cls.append(CLS_SLOW)
    return cls


def byte_segments(bit_cls: list[int], nbits: int) -> list[Field]:
    """Coalesce into byte-granular segments (mirror sc_fieldmap_from_diff)."""
    fields = []
    nbytes = (nbits + 7) // 8
    for byte in range(nbytes):
        base = byte * 8
        bits = bit_cls[base:base + 8]
        if all(c == CLS_STATIC for c in bits):
            c = CLS_STATIC
        elif any(c == CLS_COUNTER for c in bits):
            c = CLS_COUNTER
        else:
            c = CLS_SLOW
        length = 8 if base + 8 <= nbits else nbits - base
        fields.append(Field(f"byte{byte}", base, length, c))
    return fields


# --- checksum family search (mirror sc_crc / sc_checksum_search) ---

def _crc8(msg: bytes, poly: int, init: int) -> int:
    crc = init
    for byte in msg:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


_CRC_POLYS = (0x07, 0x31, 0x1D, 0x9B, 0x2F)


def checksum_search(data: bytes, target: int):
    """Return (kind, poly, init) if a common checksum of `data` equals `target`, else None.
    Tries XOR, SUM, then CRC-8 over common polynomials (mirror sc_checksum_search order)."""
    if _xor(data) == target:
        return (CK_XOR, 0, 0)
    if sum(data) & 0xFF == target:
        return (CK_SUM, 0, 0)
    for poly in _CRC_POLYS:
        for init in (0x00, 0xFF):
            if _crc8(data, poly, init) == target:
                return (CK_CRC8, poly, init)
    return None


def _xor(data: bytes) -> int:
    x = 0
    for b in data:
        x ^= b
    return x


def discover_checksum(frames: list[bytes], nbytes: int):
    """A trailing checksum byte consistent across the WHOLE corpus (System §7b layer 2)."""
    if len(frames) < 2 or nbytes < 2:
        return None
    spec = checksum_search(frames[0][:nbytes - 1], frames[0][nbytes - 1])
    if spec is None:
        return None
    for f in frames:
        if _apply(spec, f[:nbytes - 1]) != f[nbytes - 1]:
            return None
    return spec


def _apply(spec, data: bytes) -> int:
    kind, poly, init = spec
    if kind == CK_XOR:
        return _xor(data)
    if kind == CK_SUM:
        return sum(data) & 0xFF
    if kind == CK_CRC8:
        return _crc8(data, poly, init)
    return 0


# --- emit the shared .fmap format (mirror sc_fieldmap_emit) ---

def _tok(s: str) -> str:
    return s.replace(" ", "_") if s else "-"


def emit_fmap(m: FieldMap) -> str:
    lines = ["SC_FIELDMAP v1", f"protocol {_tok(m.protocol)}",
             f"modulation {m.modulation}", f"nbits {m.nbits}"]
    for f in m.fields:
        lines.append(f"field {_tok(f.name)} {f.start_bit} {f.length} {_CLS_STR[f.cls]} -")
    if m.checksum_kind != CK_NONE:
        lines.append(f"checksum {m.checksum_kind} {m.checksum_poly} {m.checksum_init} 0 0 "
                     f"{m.checksum_over_bytes}")
    lines.append("source proposed")
    return "\n".join(lines) + "\n"


# --- place-corpus driver ---

def _slug(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9]", "_", name) or "unknown"


def discover_place(place_dir: Path, min_frames: int = 2) -> list[FieldMap]:
    """Group a Zero place's .sub captures by frequency and propose a field-map per group."""
    captures = place_dir / "captures"
    if not captures.is_dir():
        return []
    by_freq: dict[int, list[tuple[bytes, int]]] = {}
    for sub in sorted(captures.glob("*.sub")):
        timings, freq, _preset = parse_sub(sub.read_text(errors="ignore"))
        if not timings:
            continue
        frame, nbits = slice_bits(timings, _unit(timings))
        by_freq.setdefault(freq, []).append((frame, nbits))

    proposals: list[FieldMap] = []
    for freq, rows in sorted(by_freq.items()):
        if len(rows) < min_frames:
            continue
        nbits = min(nb for _, nb in rows)
        nbytes = nbits // 8
        if nbytes < 1:
            continue
        frames = [f[:nbytes] for f, _ in rows]
        cls = differential(frames, nbits)
        fmap = FieldMap(protocol=f"{freq // 1000}k", nbits=nbits,
                        fields=byte_segments(cls, nbits))
        spec = discover_checksum(frames, nbytes)
        if spec is not None and fmap.fields:
            fmap.fields[-1].cls = CLS_CHECKSUM
            fmap.checksum_kind, fmap.checksum_poly, fmap.checksum_init = spec
            fmap.checksum_over_bytes = nbytes - 1
        proposals.append(fmap)
    return proposals


def write_proposals(signatures_dir: Path, proposals: list[FieldMap]) -> list[Path]:
    out_dir = signatures_dir / "field_maps"
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for m in proposals:
        path = out_dir / f"{_slug(m.protocol)}.fmap"
        path.write_text(emit_fmap(m))
        written.append(path)
    return written
