"""Parse the structured `SC key=value` FURI_LOG convention (Debug §1.3).

Every target logs greppable lines with a stable prefix + key=value form so the harness can
assert on behavior as text without brittle parsing, e.g.:

    SC scene=review action=label device=acurite match=weather conf=0.82

`parse_log_line` extracts those into a dict; `find`/`assert_line` help tests wait for and
assert on events over a captured log stream. Pure + testable (no serial needed here).
"""

from __future__ import annotations

import re
from dataclasses import dataclass

PREFIX = "SC"

# FURI_LOG lines often arrive with a leader like "42839 [I][SubCensus] SC scene=..."; find
# the "SC " marker anywhere and parse from there.
_MARKER = re.compile(r"(?:^|\s)" + re.escape(PREFIX) + r"\s+(.*)$")
_KV = re.compile(r"(\w[\w.\-]*)=(\"[^\"]*\"|\S+)")


@dataclass(frozen=True)
class LogEvent:
    raw: str
    fields: dict[str, str]

    def get(self, key: str, default: str | None = None) -> str | None:
        return self.fields.get(key, default)


def parse_log_line(line: str) -> LogEvent | None:
    """Parse one line. Returns a LogEvent if it carries the `SC ` marker, else None."""
    m = _MARKER.search(line)
    if not m:
        return None
    body = m.group(1)
    fields: dict[str, str] = {}
    for k, v in _KV.findall(body):
        if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
            v = v[1:-1]
        fields[k] = v
    return LogEvent(raw=line.rstrip("\r\n"), fields=fields)


def parse_log(text: str) -> list[LogEvent]:
    events = []
    for line in text.splitlines():
        ev = parse_log_line(line)
        if ev is not None:
            events.append(ev)
    return events


def find(events: list[LogEvent], **match: str) -> LogEvent | None:
    """First event whose fields match all of the given key=value pairs."""
    for ev in events:
        if all(ev.fields.get(k) == v for k, v in match.items()):
            return ev
    return None


def assert_line(events: list[LogEvent], **match: str) -> LogEvent:
    ev = find(events, **match)
    if ev is None:
        raise AssertionError(f"no SC log event matching {match}")
    return ev
