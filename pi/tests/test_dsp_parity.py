"""Cross-tool parity: the Python DSP port must produce the SAME golden values as the C core.

Mirrors the assertions in test/core/test_*.c against the SAME test/fixtures, so a Zero place
and a Pi place stay interchangeable (System §7 binding). If these drift, the tools have diverged.
"""

from subcensuspi.dsp import cadence, crc, diff, feature, knn, occupancy, pulse, sub

CHECK = b"123456789"


# --- crc (test_crc.c) ---

def test_crc_vectors():
    assert crc.reflect8(0x01) == 0x80
    assert crc.reflect8(0x80) == 0x01
    assert crc.reflect8(0xF0) == 0x0F
    assert crc.crc8(CHECK, 0x07, 0x00) == 0xF4  # CRC-8/SMBUS
    assert crc.crc8le(CHECK, 0x31, 0x00) == 0xA1  # CRC-8/MAXIM
    assert crc.xor_bytes(CHECK) == 0x31
    assert crc.add_bytes(CHECK) == 0xDD


def test_crc_lfsr_and_search():
    d1 = crc.lfsr_digest8(CHECK, 0x98, 0x3E)
    assert d1 == crc.lfsr_digest8(CHECK, 0x98, 0x3E)
    assert crc.lfsr_digest8(b"\x30" + CHECK[1:], 0x98, 0x3E) != d1
    payload = bytes([0xDE, 0xAD, 0xBE, 0xEF])
    spec = crc.checksum_search(payload, crc.crc8(payload, 0x07, 0x00))
    assert spec is not None and spec.kind == crc.ChecksumKind.CRC8 and spec.poly == 0x07
    spec = crc.checksum_search(payload, crc.xor_bytes(payload))
    assert spec is not None and spec.kind == crc.ChecksumKind.XOR


# --- pulse (test_pulse.c) ---

def test_pulse_clusters():
    t = [300, -300, 300, -300, 300, -300, 900, -900, 900]
    c = pulse.cluster(t, 0.25, 3)
    assert len(c) == 2
    assert c[0].count == 6 and abs(c[0].center_us - 300) <= 15
    assert c[1].count == 3 and abs(c[1].center_us - 900) <= 45
    j = pulse.cluster([0, 310, -290, 305, -295, 0, 300], 0.25, 3)
    assert len(j) == 1 and j[0].count == 5


# --- feature (test_feature.c + test_fixtures.c) ---

def _build_frames():
    frame = [300, -300, 300, -300, 300, -300, 300, -300,
             900, -300, 300, -900, 900, -300]
    buf = []
    for _ in range(3):
        buf += frame
        buf.append(-8850)
    return buf


def test_feature_synthetic():
    buf = _build_frames()
    assert len(buf) == 45
    fv = feature.compute(buf, 433918000, feature.MOD_OOK)
    assert fv.freq_bin == 433920000
    assert fv.modulation == "OOK"
    assert fv.n_symbols == 45
    assert len(fv.sym_dur_us) == 3
    assert abs(fv.sym_dur_us[0] - 300) <= 20
    assert abs(fv.sym_dur_us[1] - 900) <= 60
    assert abs(fv.est_bitrate - 3333) <= 60
    assert fv.preamble_len == 8
    assert fv.repeat_count == 3


def test_feature_fixture_matches_c_golden(fixtures_dir):
    text = (fixtures_dir / "sub" / "synth_ook_remote_433.sub").read_text(encoding="utf-8")
    parsed = sub.parse(text)
    assert parsed.frequency == 433920000
    fv = feature.compute(parsed.timings, parsed.frequency, feature.MOD_OOK)
    # same golden values as test/core/test_fixtures.c
    assert fv.freq_bin == 433920000
    assert fv.repeat_count == 5
    assert fv.preamble_len == 8
    assert len(fv.sym_dur_us) == 3
    assert abs(fv.sym_dur_us[0] - 350) <= 30
    assert abs(fv.sym_dur_us[1] - 1050) <= 90
    assert 2500 < fv.est_bitrate < 3200


# --- cadence (test_cadence.c) ---

def test_cadence():
    assert cadence.from_timestamps([1000]).cls == cadence.SEEN_ONCE
    per = cadence.from_timestamps([0, 60, 120, 180, 240, 300])
    assert per.cls == cadence.PERIODIC
    assert abs(per.period_s - 60) <= 2
    assert per.regularity > 0.9
    drop = cadence.from_timestamps([0, 60, 180, 240, 300, 420])
    assert abs(drop.period_s - 60) <= 5
    assert drop.cls in (cadence.PERIODIC, cadence.QUASI_PERIODIC)
    evt = cadence.from_timestamps([0, 5, 7, 100, 103, 500, 505])
    assert evt.cls == cadence.EVENT_DRIVEN and evt.period_s == 0.0
    cont = cadence.from_timestamps([0, 1, 2, 3, 4, 5])
    assert cont.cls == cadence.NEAR_CONTINUOUS


# --- knn (test_knn.c) ---

def _fv(bin_, mod, s0, s1, nsym, bitrate, pre, rep):
    return feature.FeatureVector(
        freq_bin=bin_, modulation=mod, sym_dur_us=[s0, s1],
        n_symbols=nsym, est_bitrate=bitrate, preamble_len=pre, repeat_count=rep,
    )


def test_knn_gating_and_cadence():
    BIN = 433920000
    cands = [
        knn.Fingerprint(_fv(BIN, "OOK", 300, 900, 45, 3333, 8, 3), cadence.PERIODIC, 60, "weather", "weather"),
        knn.Fingerprint(_fv(BIN, "OOK", 200, 600, 20, 5000, 2, 1), "", 0, "remote", "remote"),
        knn.Fingerprint(_fv(BIN, "2-FSK", 300, 900, 45, 3333, 8, 3), "", 0, "tpms", "tpms"),
        knn.Fingerprint(_fv(315000000, "OOK", 300, 900, 45, 3333, 8, 3), "", 0, "other", "remote"),
    ]
    q = knn.KnnQuery(_fv(BIN, "OOK", 305, 890, 44, 3300, 8, 3))
    m = knn.match(q, cands, 4)
    assert len(m) == 2  # only same-band, same-mod survive the gate
    assert m[0].index == 0 and m[1].index == 1
    assert m[0].confidence > m[1].confidence > 0
    assert m[0].confidence > 0.8

    qc = knn.KnnQuery(q.fv, cadence.PERIODIC, 61)
    assert knn.match(qc, cands, 4)[0].confidence >= m[0].confidence
    qd = knn.KnnQuery(q.fv, cadence.EVENT_DRIVEN, 0)
    assert knn.match(qd, cands, 4)[0].confidence < m[0].confidence


# --- diff (test_diff.c) ---

def _build_diff_frames():
    frames = []
    for i in range(8):
        b0 = 0xA5
        b1 = i
        b2 = 0x10 if i < 4 else 0x11
        b3 = crc.xor_bytes(bytes([b0, b1, b2]))
        frames.append(bytes([b0, b1, b2, b3]))
    return frames


def test_diff():
    frames = _build_diff_frames()
    prof = diff.analyze(frames, 32)
    for b in range(8):  # byte0 static
        assert prof[b].cls == diff.STATIC and prof[b].distinct == 1
    assert prof[15].cls == diff.COUNTER and abs(prof[15].change_rate - 1.0) < 1e-6
    assert abs(prof[15].entropy - 1.0) < 0.01
    assert prof[23].cls == diff.SLOW and prof[23].distinct == 2
    assert prof[16].cls == diff.STATIC
    assert any(prof[b].distinct == 2 for b in range(24, 32))
    spec = crc.checksum_search(frames[3][:3], frames[3][3])
    assert spec is not None and spec.kind == crc.ChecksumKind.XOR


# --- occupancy (test_occupancy.c) ---

def test_occupancy():
    a = occupancy.OccupancyAccum(433920000)
    rssi = [-95, -70, -68, -96, -97, -60, -94, -93, -65, -95]
    for i, r in enumerate(rssi):
        a.sample(r, -80.0, 1000 + i)
    b = a.finish()
    assert b.freq_hz == 433920000
    assert abs(b.occupancy - 0.4) < 1e-6
    assert b.crossings == 3
    assert abs(b.peak_rssi - -60) < 1e-6
    assert abs(b.noise_floor - -97) < 1e-6
    assert b.last_seen == 1008

    pass2 = occupancy.OccupancyBin(433920000, -98.0, -55.0, 0.6, 2, 2000)
    occupancy.merge(b, 10, pass2, 10)
    assert abs(b.peak_rssi - -55) < 1e-6
    assert b.crossings == 5
    assert abs(b.occupancy - 0.5) < 1e-6
    assert b.last_seen == 2000

    bins = [
        occupancy.OccupancyBin(315000000, -100, -70, 0.20, 3, 5000),
        occupancy.OccupancyBin(433920000, -98, -55, 0.50, 5, 6000),
        occupancy.OccupancyBin(915000000, -99, -60, 0.80, 9, 7000),
    ]
    wl = occupancy.watchlist_from_occupancy(bins, 0.30, 12.0)
    assert len(wl) == 2
    assert wl[0].freq_hz == 915000000 and wl[1].freq_hz == 433920000
    assert abs(wl[0].threshold_dbm - -87.0) < 1e-6
