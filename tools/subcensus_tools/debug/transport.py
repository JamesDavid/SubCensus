"""Serial transport for the Flipper RPC (Debug §2.2, §2.4).

The Flipper exposes RPC over serial as length-delimited protobuf (the same interface
qFlipper / lab.flipper.net use). This module provides:
  - varint helpers (the protobuf delimited framing) — pure + testable
  - RpcTransport: a minimal read/write abstraction
  - FakeTransport: in-memory loopback for hardware-free tests
  - PySerialTransport: real serial (lazy-imports pyserial; `pip install .[serial]`)
  - RpcSession: high-level ops (screenshot / input / logs)

RF/hardware boundary: the concrete protobuf message *encoding* is pinned to the firmware
actually installed (Debug §2.4 — "RPC/protobuf details can shift on custom firmware"). It
requires the firmware's generated protobuf classes (flipperzero-protobuf) and a real device,
so those methods are marked TODO(hw). Everything that operates on already-captured bytes
(framebuffer decode/render in framebuffer.py, log parsing in log.py) is fully offline-testable
now — which is the point of building the helper first.
"""

from __future__ import annotations

import abc


# --- protobuf delimited framing (pure, testable) ---

def encode_varint(value: int) -> bytes:
    """Base-128 varint encoding (protobuf length prefix)."""
    if value < 0:
        raise ValueError("varint must be non-negative")
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def decode_varint(data: bytes, offset: int = 0) -> tuple[int, int]:
    """Decode a varint at `offset`. Returns (value, next_offset)."""
    result = 0
    shift = 0
    pos = offset
    while True:
        if pos >= len(data):
            raise ValueError("truncated varint")
        b = data[pos]
        result |= (b & 0x7F) << shift
        pos += 1
        if not (b & 0x80):
            return result, pos
        shift += 7
        if shift > 63:
            raise ValueError("varint too long")


# --- transport abstraction ---

class RpcTransport(abc.ABC):
    @abc.abstractmethod
    def write(self, data: bytes) -> None: ...

    @abc.abstractmethod
    def read(self, n: int, timeout: float | None = None) -> bytes: ...

    def close(self) -> None:  # optional override
        pass


class FakeTransport(RpcTransport):
    """In-memory loopback for tests. `feed()` queues bytes for read(); writes are recorded."""

    def __init__(self, to_read: bytes = b"") -> None:
        self._rx = bytearray(to_read)
        self.written = bytearray()

    def feed(self, data: bytes) -> None:
        self._rx.extend(data)

    def write(self, data: bytes) -> None:
        self.written.extend(data)

    def read(self, n: int, timeout: float | None = None) -> bytes:
        take = bytes(self._rx[:n])
        del self._rx[:n]
        return take


class PySerialTransport(RpcTransport):
    """Real serial transport. Lazy-imports pyserial so the rest of the harness needs no
    serial stack. Pin the port to the Flipper's CDC device (Debug §2.4)."""

    def __init__(self, port: str, baudrate: int = 230400, timeout: float = 1.0) -> None:
        try:
            import serial  # type: ignore
        except ImportError as e:  # pragma: no cover - needs the extra installed
            raise RuntimeError(
                "pyserial not installed; run `pip install .[serial]` in tools/"
            ) from e
        self._serial = serial.Serial(port=port, baudrate=baudrate, timeout=timeout)

    def write(self, data: bytes) -> None:  # pragma: no cover - needs hardware
        self._serial.write(data)

    def read(self, n: int, timeout: float | None = None) -> bytes:  # pragma: no cover
        return self._serial.read(n)

    def close(self) -> None:  # pragma: no cover
        self._serial.close()


class RpcSession:
    """High-level RPC ops over a transport.

    The protobuf message construction is pinned to the installed firmware and needs the
    device — those methods are TODO(hw). The framing helpers and transport are exercised in
    tests via FakeTransport.
    """

    def __init__(self, transport: RpcTransport) -> None:
        self.transport = transport

    def send_frame(self, payload: bytes) -> None:
        """Send a length-delimited RPC frame (varint length prefix + payload)."""
        self.transport.write(encode_varint(len(payload)) + payload)

    def read_frame(self, timeout: float | None = None) -> bytes:
        """Read one length-delimited frame. Reads the varint prefix a byte at a time."""
        prefix = bytearray()
        while True:
            b = self.transport.read(1, timeout)
            if not b:
                raise TimeoutError("no RPC frame available")
            prefix.append(b[0])
            if not (b[0] & 0x80):
                break
        length, _ = decode_varint(bytes(prefix))
        return self.transport.read(length, timeout)

    # --- device ops (need the firmware protobuf + a real Flipper) ---

    def screenshot(self) -> bytes:  # pragma: no cover - hardware
        raise NotImplementedError(
            "TODO(hw): build a Gui ScreenFrame request with the pinned firmware's protobuf "
            "(flipperzero-protobuf), send via send_frame(), decode the 1024-byte framebuffer "
            "from the response, then render with framebuffer.decode_framebuffer()."
        )

    def send_input(self, events) -> None:  # pragma: no cover - hardware
        raise NotImplementedError(
            "TODO(hw): map InputEvents to the firmware's InputKey/InputType protobuf enums "
            "and send Gui SendInputEvent frames."
        )
