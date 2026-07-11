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
import time

_PB = None


def _load_pb():
    """Lazy-load the compiled Flipper protobuf (proto/*_pb2.py). Kept out of import time so the
    framing/parse helpers stay dependency-free for the offline tests."""
    global _PB
    if _PB is None:
        import sys
        from pathlib import Path
        proto_dir = str(Path(__file__).resolve().parent / "proto")
        if proto_dir not in sys.path:
            sys.path.insert(0, proto_dir)
        import flippermin_pb2  # noqa: E402  (generated)
        import gui_pb2  # noqa: E402  (generated)
        _PB = (flippermin_pb2, gui_pb2)
    return _PB


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

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 1.0) -> None:
        # 115200: the Flipper CDC is baud-agnostic for data, but 230400 trips a Windows
        # ClearCommError on big reads with some drivers — 115200 is the safe, tested value.
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
        if timeout is not None and timeout != self._serial.timeout:
            self._serial.timeout = timeout  # honor per-call timeout (short reads for draining)
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
        self._cmd_id = 0
        self._rpc_started = False

    def _next_id(self) -> int:
        self._cmd_id += 1
        return self._cmd_id

    def send_frame(self, payload: bytes) -> None:
        """Send a length-delimited RPC frame (varint length prefix + payload)."""
        self.transport.write(encode_varint(len(payload)) + payload)

    def _read_exact(self, n: int, timeout: float | None = None) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.transport.read(n - len(buf), timeout)
            if not chunk:
                raise TimeoutError(f"short RPC read: {len(buf)}/{n} bytes")
            buf.extend(chunk)
        return bytes(buf)

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
        return self._read_exact(length, timeout)

    def drain(self, timeout: float = 0.25) -> int:
        """Consume any pending response frames (e.g. the command-status ACKs each input request
        gets) so a following screenshot's frame reader stays in sync. Returns #frames drained."""
        n = 0
        while True:
            try:
                self.read_frame(timeout=timeout)
                n += 1
            except (TimeoutError, ValueError):
                return n

    # --- device ops (need the firmware protobuf + a real Flipper) ---

    def start_rpc(self) -> None:
        """Enter the Flipper's RPC mode from the text CLI (`start_rpc_session`). After this the
        channel is length-delimited protobuf (Debug §2.4). Idempotent."""
        if self._rpc_started:
            return
        self.transport.write(b"\r")           # finish any half-typed line
        time.sleep(0.15)
        self.transport.read(8192, 0.3)        # drain the prompt/banner
        self.transport.write(b"start_rpc_session\r")
        time.sleep(0.4)
        self.transport.read(8192, 0.6)        # drain the echoed command; RPC is now active
        self._rpc_started = True

    def send_input(self, events) -> None:
        """Inject Gui input events (InputEvent list) over RPC — drives the on-device UI. A real
        button emits PRESS -> (SHORT|LONG) -> RELEASE; the firmware's GUI ignores a lone SHORT, so
        a SHORT/LONG event is expanded to that full sequence. Explicit PRESS/RELEASE/REPEAT are
        sent verbatim for fine control."""
        pb, gui = _load_pb()
        self.start_rpc()
        for ev in events:
            name = ev.type.name
            seq = ["PRESS", name, "RELEASE"] if name in ("SHORT", "LONG") else [name]
            for tname in seq:
                m = pb.Main(command_id=self._next_id())
                m.gui_send_input_event_request.key = gui.InputKey.Value(ev.key.name)
                m.gui_send_input_event_request.type = gui.InputType.Value(tname)
                self.send_frame(m.SerializeToString())
                time.sleep(0.02)
            time.sleep(0.06)
        self.drain()  # consume the command-status ACKs so a later screenshot stays in sync

    def screenshot(self) -> bytes:
        """Grab one 1024-byte framebuffer: start the screen stream, read until a ScreenFrame
        arrives, stop the stream. Decode with framebuffer.decode_framebuffer()."""
        pb, _gui = _load_pb()
        self.start_rpc()
        self.drain()  # clear any stale ACKs so we read the ScreenFrame, not a leftover response
        start = pb.Main(command_id=self._next_id())
        start.gui_start_screen_stream_request.SetInParent()
        self.send_frame(start.SerializeToString())
        data = None
        for _ in range(12):  # skip any command-status acks until the frame
            try:
                msg = pb.Main.FromString(self.read_frame(timeout=2.0))
            except TimeoutError:
                break
            if msg.WhichOneof("content") == "gui_screen_frame":
                data = bytes(msg.gui_screen_frame.data)
                break
        stop = pb.Main(command_id=self._next_id())
        stop.gui_stop_screen_stream_request.SetInParent()
        self.send_frame(stop.SerializeToString())
        if data is None:
            raise TimeoutError("no ScreenFrame received (is an app in the foreground?)")
        return data
