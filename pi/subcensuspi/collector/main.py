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
from .rtl433 import (
    prune_samples,
    replay_cu8,
    replay_file,
    rtl433_available,
    stream_live,
    supervise_stream,
)

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
    from ..brain import Brain

    brain = Brain(cfg.signatures_dir)  # System §6: protocol_map lookup on each live decode
    log.info("SC event=brain_loaded protocol_map_rows=%d dir=%s", len(brain), cfg.signatures_dir)
    collector = Collector(
        db,
        place=cfg.place,
        capture_unknowns=cfg.capture_unknowns,
        iq_dir=cfg.place_iq_dir(),  # per-place captured IQ (§9a), not the flat iq_dir
        max_iq_gb=cfg.max_iq_gb,
        mqtt=mqtt,
        brain=brain,
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
    # Raw-burst evidence trail (§6): every detected transmission saved as a .cu8 in the place iq
    # dir, so any burst can be re-matched against every decoder later. Rolling window — pruned at
    # launch (stream_live) and every 10 min by the janitor, capped at cfg.max_samples_gb.
    capture_dir = cfg.place_iq_dir() if cfg.capture_samples else None
    import threading
    import time as _time

    def _janitor() -> None:  # pragma: no cover - timing loop
        # Housekeeping every 10 min: bound the sample window + events table, keep cadence_* fresh
        # (§7a) so the dashboard's Devices list shows live cadence instead of NULL. Its own DB
        # handle — SQLite WAL lets the consumer keep writing.
        jdb = Database(args.db or cfg.db_path)
        while True:
            _time.sleep(600)
            try:
                if capture_dir:
                    prune_samples(capture_dir, max_gb=cfg.max_samples_gb)
                if cfg.max_events > 0:
                    jdb.prune_events(cfg.max_events)
                jdb.refresh_all_cadences()
            except Exception as exc:  # pragma: no cover - never let housekeeping kill the census
                log.warning("SC event=janitor_error err=%s", exc)

    threading.Thread(target=_janitor, daemon=True).start()
    streams = [
        SourceStream(
            supervise_stream(lambda d=d: stream_live(d, capture_dir=capture_dir)),
            d.serial or ",".join(d.freqs),
        )
        for d in dongles
    ]
    run_multi(collector, streams)


if __name__ == "__main__":
    raise SystemExit(main())
