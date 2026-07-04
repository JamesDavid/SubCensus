"""Tests for the Flipper serial-RPC debug harness (Debug §2.2).

Pure logic only — framebuffer decode/render, input parsing, log parsing, RPC framing.
No device attached (that is the point of building the helper first)."""

import struct
import zlib

import pytest

from subcensus_tools.debug import framebuffer as fb
from subcensus_tools.debug.input import (
    InputKey,
    InputType,
    parse_event,
    parse_sequence,
)
from subcensus_tools.debug.log import parse_log, parse_log_line
from subcensus_tools.debug.transport import (
    FakeTransport,
    RpcSession,
    decode_varint,
    encode_varint,
)


# --- framebuffer ---

def _set_pixel(buf: bytearray, x: int, y: int, w: int = fb.WIDTH):
    buf[(y // 8) * w + x] |= 1 << (y % 8)


def test_framebuffer_decode_layout():
    buf = bytearray(fb.FRAME_BYTES)
    _set_pixel(buf, 0, 0)     # top-left
    _set_pixel(buf, 127, 63)  # bottom-right
    _set_pixel(buf, 10, 9)    # page 1, bit 1
    px = fb.decode_framebuffer(bytes(buf))
    assert px[0][0] == 1
    assert px[63][127] == 1
    assert px[9][10] == 1
    assert px[0][1] == 0


def test_framebuffer_too_small_raises():
    with pytest.raises(ValueError):
        fb.decode_framebuffer(b"\x00" * 10)


def test_ascii_render_dimensions():
    buf = bytearray(fb.FRAME_BYTES)
    _set_pixel(buf, 5, 0)
    px = fb.decode_framebuffer(bytes(buf))
    art = fb.to_ascii(px, on="#", off=".")
    lines = art.splitlines()
    assert len(lines) == 64
    assert len(lines[0]) == 128
    assert lines[0][5] == "#"
    assert lines[0][4] == "."


def test_halfblock_halves_rows():
    px = fb.decode_framebuffer(bytes(fb.FRAME_BYTES))
    assert len(fb.to_ascii_halfblock(px).splitlines()) == 32


def test_png_is_valid_and_correct_size():
    buf = bytearray(fb.FRAME_BYTES)
    _set_pixel(buf, 0, 0)
    px = fb.decode_framebuffer(bytes(buf))
    png = fb.to_png(px, scale=3)
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    # IHDR width/height reflect the scale
    w, h = struct.unpack(">II", png[16:24])
    assert (w, h) == (128 * 3, 64 * 3)
    # IDAT decompresses to scanlines of (1 filter byte + width bytes) * height
    start = png.index(b"IDAT") + 4
    length = struct.unpack(">I", png[png.index(b"IDAT") - 4:png.index(b"IDAT")])[0]
    raw = zlib.decompress(png[start:start + length])
    assert len(raw) == (128 * 3) * (64 * 3) + (64 * 3)  # +1 filter byte per row


def test_cursor_row_finds_filled_bar():
    buf = bytearray(fb.FRAME_BYTES)
    for x in range(fb.WIDTH):  # fully fill row 20 (an inverted selection bar)
        _set_pixel(buf, x, 20)
    px = fb.decode_framebuffer(bytes(buf))
    assert fb.cursor_row(px) == 20


# --- input ---

def test_parse_event_variants():
    assert parse_event("Down") == parse_event("d")
    assert parse_event("Down").key == InputKey.DOWN
    assert parse_event("Down").type == InputType.SHORT
    assert parse_event("long:ok").type == InputType.LONG
    assert parse_event("long:ok").key == InputKey.OK
    assert parse_event("ok:long").type == InputType.LONG


def test_parse_sequence():
    seq = parse_sequence("Down Down Ok")
    assert [e.key for e in seq] == [InputKey.DOWN, InputKey.DOWN, InputKey.OK]
    seq2 = parse_sequence("down, long:ok, back")
    assert [e.key for e in seq2] == [InputKey.DOWN, InputKey.OK, InputKey.BACK]
    assert seq2[1].type == InputType.LONG


def test_parse_bad_key_raises():
    with pytest.raises(ValueError):
        parse_event("wiggle")


# --- log parsing ---

def test_parse_log_line_with_furi_leader():
    line = "42839 [I][SubCensus] SC scene=review action=label device=acurite conf=0.82"
    ev = parse_log_line(line)
    assert ev is not None
    assert ev.get("scene") == "review"
    assert ev.get("action") == "label"
    assert ev.get("conf") == "0.82"


def test_parse_log_ignores_non_sc_lines():
    assert parse_log_line("some random firmware chatter") is None


def test_parse_log_quoted_values():
    ev = parse_log_line('SC scene=camp note="hello world" freq=433920000')
    assert ev.get("note") == "hello world"
    assert ev.get("freq") == "433920000"


def test_parse_log_multiline_and_find():
    text = "\n".join([
        "boot",
        "SC scene=menu",
        "SC scene=sweep freq=433920000 rssi=-62",
        "SC scene=review action=label label=weather",
    ])
    events = parse_log(text)
    assert len(events) == 3
    from subcensus_tools.debug.log import find
    assert find(events, scene="sweep") is not None
    assert find(events, action="label").get("label") == "weather"
    assert find(events, scene="nope") is None


# --- rpc framing ---

@pytest.mark.parametrize("value", [0, 1, 127, 128, 300, 16384, 1 << 20])
def test_varint_roundtrip(value):
    enc = encode_varint(value)
    dec, off = decode_varint(enc)
    assert dec == value
    assert off == len(enc)


def test_rpc_frame_roundtrip_over_fake_transport():
    t = FakeTransport()
    session = RpcSession(t)
    payload = b"\x08\x96\x01hello"
    session.send_frame(payload)
    # what got written is a valid framed message; feed it back and read it
    t.feed(bytes(t.written))
    got = session.read_frame()
    assert got == payload
