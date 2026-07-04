"""Checksum / CRC family (System §7b tier 2) — Python port of shared/core/sc_crc.c.

Bit-exact with the C core. Verified against the same golden vectors (test/core/test_crc.c).
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

# Common CRC-8 polynomials seen across ISM device decoders (mirror of sc_crc.c).
_CRC8_POLYS = (0x07, 0x31, 0x1D, 0x2F, 0x9B, 0xD5, 0x8D, 0x9C)
_CRC8_INITS = (0x00, 0xFF)


def reflect8(x: int) -> int:
    x &= 0xFF
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4)
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2)
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1)
    return x & 0xFF


def crc8(msg: bytes, poly: int, init: int) -> int:
    r = init & 0xFF
    for b in msg:
        r ^= b
        for _ in range(8):
            r = ((r << 1) ^ poly) & 0xFF if (r & 0x80) else (r << 1) & 0xFF
    return r & 0xFF


def crc8le(msg: bytes, poly: int, init: int) -> int:
    r = reflect8(init)
    p = reflect8(poly)
    for b in msg:
        r ^= b
        for _ in range(8):
            r = ((r >> 1) ^ p) & 0xFF if (r & 0x01) else (r >> 1) & 0xFF
    return r & 0xFF


def xor_bytes(msg: bytes) -> int:
    r = 0
    for b in msg:
        r ^= b
    return r & 0xFF


def add_bytes(msg: bytes) -> int:
    r = 0
    for b in msg:
        r = (r + b) & 0xFF
    return r


def lfsr_digest8(msg: bytes, gen: int, key: int) -> int:
    total = 0
    k = key & 0xFF
    for data in msg:
        for i in range(7, -1, -1):
            if (data >> i) & 1:
                total ^= k
            k = ((k >> 1) ^ gen) & 0xFF if (k & 1) else (k >> 1) & 0xFF
    return total & 0xFF


class ChecksumKind(str, Enum):
    NONE = "none"
    XOR = "xor"
    SUM = "sum"
    CRC8 = "crc8"
    CRC8LE = "crc8le"
    LFSR8 = "lfsr8"


@dataclass
class ChecksumSpec:
    kind: ChecksumKind
    poly: int = 0
    init: int = 0
    gen: int = 0
    key: int = 0


def checksum_search(data: bytes, target: int) -> ChecksumSpec | None:
    """Brute the common family against `target`; return the first match or None (§7b)."""
    if xor_bytes(data) == target:
        return ChecksumSpec(ChecksumKind.XOR)
    if add_bytes(data) == target:
        return ChecksumSpec(ChecksumKind.SUM)
    for poly in _CRC8_POLYS:
        for init in _CRC8_INITS:
            if crc8(data, poly, init) == target:
                return ChecksumSpec(ChecksumKind.CRC8, poly=poly, init=init)
            if crc8le(data, poly, init) == target:
                return ChecksumSpec(ChecksumKind.CRC8LE, poly=poly, init=init)
    return None
