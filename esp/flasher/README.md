# SubCensusEsp — browser web flasher

A one-click, no-toolchain flasher for the ESP32 + CC1101 node, built on
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) (Web Serial). It flashes a **merged
image** (bootloader + partitions + app + the LittleFS web UI) so a fresh board comes up fully
working. Includes CC1101 wiring, first-boot setup, and troubleshooting.

- `index.html` — the flasher page + documentation (this is what GitHub Pages serves).
- `manifest.json` — ESP Web Tools manifest (ESP32, one merged part at offset 0).
- `build_merged.py` — builds the firmware + filesystem and merges them into
  `firmware/subcensusesp.bin`.
- `firmware/` — the merged image (git-ignored; produced by CI or `build_merged.py`).

Browser support: **Chrome / Edge on desktop** (Web Serial isn't available in Firefox, Safari,
or mobile browsers).

## Publish it (GitHub Pages, one-time)

The image is built and deployed by CI, so nothing binary is committed and the flasher never
drifts from the source:

1. Repo **Settings → Pages → Build and deployment → Source: “GitHub Actions.”**
2. Push to `master` (any change under `esp/` or `shared/core/`). The workflow
   `.github/workflows/deploy-flasher.yml` builds `esp/`, merges the image, and deploys.
3. The flasher is live at `https://<user>.github.io/<repo>/` — e.g.
   **https://jamesdavid.github.io/SubCensus/**.

Trigger a build without a code change from the Actions tab (**Run workflow**).

## Build / test it locally

```
pip install platformio
python esp/flasher/build_merged.py        # -> esp/flasher/firmware/subcensusesp.bin
# serve the flasher over http (Web Serial needs https OR http://localhost)
python -m http.server -d esp/flasher 8000 # open http://localhost:8000
```

The offsets baked into the merge (esp32dev default OTA partition table):

| region | offset |
|---|---|
| bootloader | `0x1000` |
| partitions | `0x8000` |
| boot_app0 | `0xe000` |
| app (firmware) | `0x10000` |
| littlefs (web UI) | `0x290000` |

## CLI flash (no browser)

```
cd esp
python -m platformio run -e esp32dev -t upload     # firmware over USB
python -m platformio run -e esp32dev -t uploadfs   # web UI (LittleFS)
# or flash the merged image directly:
python -m esptool --chip esp32 write_flash 0x0 esp/flasher/firmware/subcensusesp.bin
```

## Wiring the CC1101

Full pin map + diagram are on the flasher page (`index.html`). Summary (VSPI):

| CC1101 | ESP32 GPIO | note |
|---|---|---|
| VCC | 3V3 | **3.3 V only — not 5 V**; no level shifter |
| GND | GND | |
| SCK | 18 | VSPI clock |
| MOSI (SI) | 23 | |
| MISO (SO) | 19 | |
| CSN (CS) | 5 | |
| GDO0 | 34 | RX data / RMT edge capture (input-only) |
| GDO2 | 35 | optional |
| ANT | — | antenna tuned to ~433 or ~915 MHz |
