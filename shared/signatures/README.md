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
