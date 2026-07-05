"""Collector CLI entry (Pi §2, §9).

  python -m subcensuspi.collector.main --config config.yaml            # live (needs dongle)
  python -m subcensuspi.collector.main --config config.yaml --replay stream.jsonl   # no hw
"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from ..config import Config
from ..db import Database
from ..mqtt import MqttPublisher
from ..occupancy_pass import read_watchlist_rows
from .collector import Collector
from .multi import SourceStream, run_multi
from .priority import prioritize_dongles
from .rtl433 import replay_cu8, replay_file, rtl433_available, stream_live, supervise_stream

log = logging.getLogger("subcensuspi.collector.main")


def _make_mqtt(cfg: Config) -> MqttPublisher | None:
    """Build + connect the HA discovery publisher when enabled (Pi §9).

    The wiring is always live; only the broker socket connect needs a running broker
    (TODO(hw)) — a failed connect degrades gracefully to no MQTT rather than aborting the
    collector, and the publish path itself is exercised by the fake-broker integration test.
    """
    if not cfg.mqtt.enabled:
        return None
    pub = MqttPublisher(cfg.mqtt)
    try:
        pub.connect()  # TODO(hw): needs a live MQTT broker; guarded so no broker != crash
    except OSError as exc:
        log.warning("SC event=mqtt_connect_failed host=%s err=%s (continuing without MQTT)",
                    cfg.mqtt.host, exc)
        return None
    log.info("SC event=mqtt_connected host=%s port=%d", cfg.mqtt.host, cfg.mqtt.port)
    return pub


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
    mqtt = _make_mqtt(cfg)
    collector = Collector(
        db,
        place=cfg.place,
        capture_unknowns=cfg.capture_unknowns,
        iq_dir=cfg.place_iq_dir(),  # per-place captured IQ (§9a), not the flat iq_dir
        max_iq_gb=cfg.max_iq_gb,
        mqtt=mqtt,
    )

    try:
        if args.replay:
            collector.process_stream(replay_file(args.replay), source="replay")
        elif args.replay_cu8:
            collector.process_stream(replay_cu8(args.replay_cu8), source="replay-cu8")
        else:
            if not rtl433_available():
                print("rtl_433 not installed and no --replay given; nothing to do.",
                      file=sys.stderr)
                return 2
            _run_live(cfg, collector)  # pragma: no cover - needs hardware
    finally:
        if mqtt is not None:
            mqtt.disconnect()

    s = collector.stats
    print(f"lines={s.lines} decoded={s.decoded} unknowns={s.unknowns} skipped={s.skipped}")
    print(f"devices={db.device_count()} events={db.event_count()}")
    db.close()
    return 0


def _run_live(cfg: Config, collector: Collector) -> None:  # pragma: no cover - needs hardware
    """Multi-dongle parallel + source tagging (Pi §3): one producer per dongle, single
    DB-writer consumer. Each producer is supervised so a dead rtl_433 relaunches with backoff
    (Pi §9). Optionally reorder hop/dongle attention by the place watchlist (§3, opt-in).
    TODO(hw): needs real dongles.
    """
    dongles = list(cfg.dongles)
    if cfg.prioritize_watchlist:
        wl = read_watchlist_rows(Path(cfg.places_dir) / cfg.place / "watchlist.csv")
        if wl:
            dongles = prioritize_dongles(dongles, wl)
            log.info("SC event=watchlist_priority applied dongles=%d", len(dongles))
    streams = [
        SourceStream(
            supervise_stream(lambda d=d: stream_live(d)),
            d.serial or ",".join(d.freqs),
        )
        for d in dongles
    ]
    run_multi(collector, streams)


if __name__ == "__main__":
    raise SystemExit(main())
