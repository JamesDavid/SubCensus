# shared/signatures/ — distributable brain seed (System §6, §8)

A ready-to-use starter for the global classification brain, so a fresh install classifies
usefully out of the box instead of starting empty.

- **`protocol_map.csv`** — decoded-protocol → friendly name + `device_class` for ~64 common
  Flipper SubGhz + rtl_433 protocols (gate/garage openers, generic remotes, weather sensors,
  TPMS, utility meters, security/motion). Keyed by the decoder's `protocol` string (differs per
  tool), so both a Flipper protocol name and an rtl_433 model resolve. Reference metadata only.
- **`fingerprints.csv`** — the feature-vector k-NN library. Ships **header-only / empty**: real
  fingerprints come from the active-learning loop (confirm a label → append the capture's
  feature vector, System §6) or from merging labeled captures. There is no useful synthetic
  fingerprint seed.
- **`field_maps/<key>.fmap`** (optional) — per-protocol frame structure: labeled bit fields +
  checksum algorithm (System §6, §7b). **Proposed, never auto-committed** — written by the Zero
  editor, the Esp web UI, or `build_signatures.py --places` (passive differential + checksum
  discovery over a place's capture corpus) with `source proposed`; the user confirms (→ `source
  user`). Read back by the structured field editor to present labeled bits and re-sign edits.

### `.fmap` file format (v1)

A small line-oriented text format (parsed/emitted on-device by `shared/core/sc_fieldmap`, and in
Python by `tools/subcensus_tools/fieldmap.py`). Tokens are space-separated; `_` encodes a space
and `-` an empty value:

```
SC_FIELDMAP v1
protocol <name>
modulation <0=OOK|1=2FSK>
nbits <N>
field <name> <start_bit> <length> <static|slow|counter|checksum|data> <semantics_or_->
checksum <kind> <poly> <init> <gen> <key> <over_bytes>
source <user|proposed>
```

`checksum <kind>` is the `ScChecksumKind` int (1=XOR, 2=SUM, 3=CRC-8, 4=CRC-8LE, 5=LFSR8);
`over_bytes` is how many leading bytes it covers. Bits are MSB-first, matching `sc_diff` /
`sc_slice`, so a differential profile maps straight onto fields.

## Deploy it

Copy these into a device's global `signatures/` dir (they're **global**, shared across places
and all three sensors):

- **Zero:** `/ext/apps_data/subcensuszero/signatures/` (SD card)
- **Pi:** the configured `signatures_dir`
- **Esp:** the node's `signatures/` (LittleFS/SD), or push via brain sync over WiFi

## Regenerate / extend

The seed is generated from the curated list in `tools/build_signatures.py` (the single merge
point, System §8):

```
python tools/build_signatures.py --signatures-dir shared/signatures
```

To add protocols, edit `SEED_PROTOCOL_MAP` in `build_signatures.py` and re-run. The same tool
merges user-labeled fingerprints from every sensor into `fingerprints.csv` — the brain gets
smarter with use (System §6). Both files are validated against `shared/schema/`.

Pass `--places <place_dir> …` to also run the passive field-map discovery over each place's
capture corpus (needs ≥2 same-frequency captures per device), proposing `field_maps/*.fmap`
entries (`source proposed`) for the user to confirm:

```
python tools/build_signatures.py --signatures-dir shared/signatures --places path/to/place
```
