"""Collector CLI entry (Pi §2, §9).

  python -m subcensuspi.collector.main --config config.yaml            # live (needs dongle)
  python -m subcensuspi.collector.main --config config.yaml --replay stream.jsonl   # no hw
"""

from __future__ import annotations

import argparse
import logging
import sys

from ..config import Config
from ..db import Database
from .collector import Collector
from .rtl433 import replay_cu8, replay_file, rtl433_available, stream_live


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--config", required=True, help="YAML config (Pi §8)")
    ap.add_argument("--replay", help="recorded rtl_433 .jsonl to feed the collector (no hw)")
    ap.add_argument("--replay-cu8", help="recorded .cu8/.ook IQ to replay via rtl_433 -r")
    ap.add_argument("--db", help="override db_path")
    args = ap.parse_args(argv)

    logging.basicConfig(level=logging.INFO, format="%(message)s")
    cfg = Config.load(args.config)
    db = Database(args.db or cfg.db_path)
    collector = Collector(db, place=cfg.place, capture_unknowns=cfg.capture_unknowns)

    if args.replay:
        collector.process_stream(replay_file(args.replay), source="replay")
    elif args.replay_cu8:
        collector.process_stream(replay_cu8(args.replay_cu8), source="replay-cu8")
    else:
        if not rtl433_available():
            print("rtl_433 not installed and no --replay given; nothing to do.", file=sys.stderr)
            return 2
        for dongle in cfg.dongles:  # pragma: no cover - needs hardware
            src = dongle.serial or ",".join(dongle.freqs)
            collector.process_stream(stream_live(dongle), source=src)

    s = collector.stats
    print(f"lines={s.lines} decoded={s.decoded} unknowns={s.unknowns} skipped={s.skipped}")
    print(f"devices={db.device_count()} events={db.event_count()}")
    db.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
