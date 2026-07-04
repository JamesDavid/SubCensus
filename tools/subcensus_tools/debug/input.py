"""Flipper virtual input model (Debug §2.2).

The input RPC injects virtual button events to navigate scenes. This module is the pure,
testable model — key/type enums + sequence parsing ("Down Down Ok" -> events). The mapping
onto the firmware's protobuf input enums happens in transport.py against the pinned SDK.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class InputKey(Enum):
    UP = "up"
    DOWN = "down"
    LEFT = "left"
    RIGHT = "right"
    OK = "ok"
    BACK = "back"


class InputType(Enum):
    PRESS = "press"      # button down
    RELEASE = "release"  # button up
    SHORT = "short"      # a completed short click
    LONG = "long"        # a completed long press
    REPEAT = "repeat"    # auto-repeat while held


_KEY_ALIASES = {
    "u": InputKey.UP, "up": InputKey.UP,
    "d": InputKey.DOWN, "down": InputKey.DOWN,
    "l": InputKey.LEFT, "left": InputKey.LEFT,
    "r": InputKey.RIGHT, "right": InputKey.RIGHT,
    "o": InputKey.OK, "ok": InputKey.OK, "enter": InputKey.OK,
    "b": InputKey.BACK, "back": InputKey.BACK, "esc": InputKey.BACK,
}


@dataclass(frozen=True)
class InputEvent:
    key: InputKey
    type: InputType = InputType.SHORT

    def __str__(self) -> str:
        return f"{self.key.value}:{self.type.value}"


def parse_key(token: str) -> InputKey:
    """Parse a key token. Supports 'Ok', 'down', 'l', and a 'long:ok' type prefix's key."""
    t = token.strip().lower()
    if ":" in t:
        t = t.split(":", 1)[1]
    if t not in _KEY_ALIASES:
        raise ValueError(f"unknown input key: {token!r}")
    return _KEY_ALIASES[t]


def parse_event(token: str) -> InputEvent:
    """Parse one event token. 'Down' -> short Down; 'long:ok' -> long Ok; 'ok:long' too."""
    t = token.strip().lower()
    itype = InputType.SHORT
    key_part = t
    if ":" in t:
        a, b = t.split(":", 1)
        # allow either order: type:key or key:type
        type_names = {e.value for e in InputType}
        if a in type_names:
            itype, key_part = InputType(a), b
        elif b in type_names:
            key_part, itype = a, InputType(b)
        else:
            key_part = b
    return InputEvent(parse_key(key_part), itype)


def parse_sequence(spec: str) -> list[InputEvent]:
    """Parse a whitespace- or comma-separated sequence, e.g. 'Down Down Ok' or
    'down, long:ok, back' into a list of InputEvents."""
    tokens = [t for t in spec.replace(",", " ").split() if t]
    return [parse_event(t) for t in tokens]
