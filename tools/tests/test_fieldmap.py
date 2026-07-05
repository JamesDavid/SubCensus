"""Passive field-map discovery over a place's capture corpus (System §7b/§8 build_signatures).

Mirrors the C test_diff/test_fieldmap corpus (byte0 static id, byte1 counter, byte2 slow, byte3
XOR checksum) so a tools-proposed .fmap matches an on-device one, and asserts build_signatures
wires it end to end. Passive — proposals only (source=proposed).
"""

from __future__ import annotations

import sys
from pathlib import Path

from subcensus_tools import fieldmap

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_signatures  # noqa: E402


def _xor(data: bytes) -> int:
    x = 0
    for b in data:
        x ^= b
    return x


def _encode_sub(frame: bytes, nbits: int, unit: int, freq: int) -> str:
    """Encode a bit frame to Flipper RAW .sub text (coalesce same-level bit runs, mirror
    sc_slice_encode) so slicing it back reproduces the frame."""
    timings: list[int] = []
    i = 0
    while i < nbits:
        level = (frame[i // 8] >> (7 - (i % 8))) & 1
        run = 0
        while i < nbits and ((frame[i // 8] >> (7 - (i % 8))) & 1) == level:
            run += 1
            i += 1
        timings.append(run * unit if level else -run * unit)
    raw = " ".join(str(t) for t in timings)
    return (f"Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: {freq}\n"
            f"Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\nRAW_Data: {raw}\n")


def _make_place(tmp: Path) -> Path:
    place = tmp / "place"
    (place / "captures").mkdir(parents=True)
    freq = 433920000
    for i in range(8):
        f = bytearray(4)
        f[0] = 0xA5           # static id
        f[1] = i              # counter
        f[2] = 0x10 if i < 4 else 0x11  # slow-varying
        f[3] = _xor(f[:3])    # xor checksum
        (place / "captures" / f"cap_{i}.sub").write_text(_encode_sub(bytes(f), 32, 100, freq))
    return place


def test_discover_place(tmp_path):
    place = _make_place(tmp_path)
    props = fieldmap.discover_place(place)
    assert len(props) == 1
    m = props[0]
    assert m.nbits == 32
    assert len(m.fields) == 4
    assert m.fields[0].cls == fieldmap.CLS_STATIC     # id byte
    assert m.fields[1].cls == fieldmap.CLS_COUNTER    # counter byte
    assert m.fields[2].cls == fieldmap.CLS_SLOW       # slow byte
    assert m.fields[3].cls == fieldmap.CLS_CHECKSUM   # checksum byte
    assert m.checksum_kind == fieldmap.CK_XOR
    assert m.checksum_over_bytes == 3


def test_emit_format(tmp_path):
    place = _make_place(tmp_path)
    props = fieldmap.discover_place(place)
    text = fieldmap.emit_fmap(props[0])
    assert text.startswith("SC_FIELDMAP v1\n")
    assert "nbits 32" in text
    assert "checksum 1 0 0 0 0 3" in text  # kind=XOR(1), over_bytes=3
    assert text.rstrip().endswith("source proposed")  # never auto-committed


def test_build_signatures_writes_field_maps(tmp_path):
    place = _make_place(tmp_path)
    sig = tmp_path / "signatures"
    rc = build_signatures.main(["--signatures-dir", str(sig), "--no-seed", "--places", str(place)])
    assert rc == 0
    fmaps = list((sig / "field_maps").glob("*.fmap"))
    assert len(fmaps) == 1
    body = fmaps[0].read_text()
    assert "source proposed" in body and "checksum 1 0 0 0 0 3" in body


def test_no_corpus_no_proposals(tmp_path):
    # a single capture per freq is not enough for a differential (needs >=2, System §7b)
    place = tmp_path / "p2"
    (place / "captures").mkdir(parents=True)
    f = bytes([0xA5, 0x00, 0x10, 0xB5])
    (place / "captures" / "one.sub").write_text(_encode_sub(f, 32, 100, 315000000))
    assert fieldmap.discover_place(place) == []
