"""Tests for the shared brain IO + merge + build_signatures (System §6, §8)."""

import sys
from pathlib import Path

from subcensus_tools import brain
from subcensus_tools.schema import load_all_schemas, validate_csv
from subcensus_tools.taxonomy import Taxonomy

# import the CLI module (tools/build_signatures.py)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_signatures  # noqa: E402


def _fp(freq_bin, mod, s1, cls, source="user", name=""):
    return {
        "id": "", "freq_bin": freq_bin, "modulation": mod, "sym_dur_us_1": s1,
        "sym_dur_us_2": "", "sym_dur_us_3": "", "n_symbols": 45, "est_bitrate": 3333,
        "preamble_len": 8, "repeat_count": 5, "device_name": name, "device_class": cls,
        "source": source, "cadence_class": "", "period_s": "", "period_regularity": "",
        "cadence_samples": "",
    }


def test_fingerprint_roundtrip_and_schema(tmp_path):
    rows = [_fp(433920000, "OOK", 350, "remote", name="Garage remote")]
    path = tmp_path / "fingerprints.csv"
    brain.write_fingerprints(rows, path)
    assert validate_csv(load_all_schemas()["fingerprints"], path, Taxonomy.load()) == []
    back = brain.read_fingerprints(path)
    assert back[0]["device_class"] == "remote"
    assert back[0]["sym_dur_us_1"] == "350"


def test_merge_dedups_and_prefers_user_source():
    a = [_fp(433920000, "OOK", 350, "remote", source="seed")]
    b = [_fp(433920000, "OOK", 350, "remote", source="user")]  # same vector, better source
    c = [_fp(315000000, "OOK", 200, "tpms", source="user")]    # distinct
    merged = brain.merge_fingerprints([a, b, c])
    assert len(merged) == 2  # the identical vector deduped
    remote = next(r for r in merged if r["device_class"] == "remote")
    assert remote["source"] == "user"  # user beats seed
    assert all(r["id"] for r in merged)  # re-ided


def test_protocol_map_lookup_and_merge():
    pm = brain.merge_protocol_map([build_signatures.SEED_PROTOCOL_MAP])
    hit = brain.lookup_protocol(pm, "Acurite-Tower")
    assert hit is not None and hit["device_class"] == "weather"
    assert brain.lookup_protocol(pm, "Nope") is None


def test_build_signatures_merges_zero_and_pi(tmp_path):
    sig = tmp_path / "signatures"
    # a "Zero" fingerprints export and a "Pi" one, overlapping on one vector
    zero = tmp_path / "zero_fp.csv"
    pi = tmp_path / "pi_fp.csv"
    brain.write_fingerprints([_fp(433920000, "OOK", 350, "remote", source="user")], zero)
    brain.write_fingerprints([
        _fp(433920000, "OOK", 350, "remote", source="user"),   # dup of zero's
        _fp(915000000, "2-FSK", 100, "energy-meter", source="user"),  # unique
    ], pi)

    rc = build_signatures.main(["--signatures-dir", str(sig), "--fingerprints", str(zero), str(pi)])
    assert rc == 0
    tax = Taxonomy.load()
    schemas = load_all_schemas()
    assert validate_csv(schemas["fingerprints"], sig / "fingerprints.csv", tax) == []
    assert validate_csv(schemas["protocol_map"], sig / "protocol_map.csv", tax) == []
    # 2 distinct fingerprints (the dup collapsed) + seeded protocol_map present
    assert len(brain.read_fingerprints(sig / "fingerprints.csv")) == 2
    assert brain.lookup_protocol(brain.read_protocol_map(sig / "protocol_map.csv"), "Schrader")

    # re-running is idempotent (still 2)
    build_signatures.main(["--signatures-dir", str(sig), "--fingerprints", str(zero), str(pi)])
    assert len(brain.read_fingerprints(sig / "fingerprints.csv")) == 2
