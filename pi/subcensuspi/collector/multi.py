"""Multi-dongle parallel collection with source tagging (Pi §3).

One rtl_433 per dongle (pinned by serial, locked to a band) gives true simultaneous coverage.
Each dongle runs in a producer thread that pushes (source, line) onto a queue; a single
consumer drains the queue into the collector, so there is exactly ONE SQLite writer (thread-
safe) while all streams merge (Pi §3: "collector merges all JSON streams, tag each event with
source dongle/band"). Works identically for recorded fixtures (tests) and live dongles (hw).
"""

from __future__ import annotations

import queue
import threading
from dataclasses import dataclass
from typing import Iterable

from .collector import Collector

_SENTINEL = object()


@dataclass
class SourceStream:
    lines: Iterable[str]
    source: str


def run_multi(collector: Collector, streams: list[SourceStream], queue_max: int = 1000) -> None:
    """Drain N producer streams concurrently into one collector (single DB writer)."""
    q: queue.Queue = queue.Queue(maxsize=queue_max)
    active = len(streams)

    def producer(stream: SourceStream) -> None:
        try:
            for line in stream.lines:
                q.put((stream.source, line))
        finally:
            q.put(_SENTINEL)

    threads = [threading.Thread(target=producer, args=(s,), daemon=True) for s in streams]
    for t in threads:
        t.start()

    done = 0
    while done < active:
        item = q.get()
        if item is _SENTINEL:
            done += 1
            continue
        source, line = item
        collector.process_line(line, source=source)

    for t in threads:
        t.join()
