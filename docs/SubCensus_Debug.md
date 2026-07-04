# SubCensus — Debug & Test Harness

How each target is built, deployed, observed, and iterated — with the goal of a **build → deploy → observe → assert → iterate** loop that an agent (Claude Code running on your machine) can run as autonomously as the hardware allows. Defers to `SubCensus_System.md` for all schemas/semantics.

The through-line: **maximize what's testable without the radio and without a screen**, because those are the two things that can't be automated away. Everything else — logic, firmware glue, UI navigation, decode/classify/log — can be driven from text + framebuffer images + recorded fixtures.

---

## 0. What can and can't be automated
| Layer | Autonomous? | How |
|---|---|---|
| Logic core (parse, k-NN, cadence, CRC, `.sub` en/decode, CSV/index, differential analysis) | **Fully** | Host-compiled unit tests + recorded fixtures — no device, no radio |
| Firmware/app glue, scenes, state machines | **Mostly** | Build + flash + serial logs + framebuffer screenshots + injected input |
| UI rendering / navigation | **Mostly** | Framebuffer screenshot (Zero) / headless web driver (Esp, Pi) |
| Decode → classify → log path | **Fully** | Replay recorded RF captures (`.sub` / `.cu8` / `rtl_433` JSON) through the pipeline |
| **Live radio: RSSI, real capture, TX/replay** | **No** | Requires real airtime with real devices — human + antenna |

The RF boundary is hard: **nothing emulates the CC1101 or RTL-SDR.** But recorded fixtures make the entire *processing* path deterministic and testable off-device — the radio only needs a human for "did a real signal actually get captured/replayed."

---

## 1. Shared foundations

### 1.1 Host-testable logic core
Factor each app so the firmware/OS-independent logic lives in modules that compile **natively on the host** (plain `gcc`/`clang` or the language's normal test runner), separate from the hardware API calls. That logic is then unit-tested here with no device attached.
- **Zero + Esp** share the same silicon (CC1101) and much of the same logic (feature vector §7, cadence §7a, k-NN §6, `.sub`/pulse en·decode, CRC/checksum, differential analysis §7b). Put that in a **shared C `core/` library** (`shared/core/` in the monorepo) compiled into both the Zero FAP and the Esp firmware, and host-compiled for tests. One core, two firmwares, one test suite.
- **Pi** is Python; it reimplements or wraps the same algorithms and tests them with `pytest`. The host-side tools (`export_place`/`analyze_place`/`build_signatures`) are already pure Python and testable directly.

### 1.2 Fixtures — the deterministic stand-in for a radio
A shared `test/fixtures/` corpus is what lets the decode/classify/log path be tested without airtime:
- **`.sub`** RAW captures (Zero/Esp) — real device recordings + synthetic frames.
- **`.cu8` / `.ook`** IQ/pulse files (Pi) — replayable with `rtl_433 -r`.
- **`rtl_433` JSON** event streams (Pi collector input).
- **Synthetic frames** with known field layouts + checksums, to test decode/edit/field-map (§7b) with ground-truth answers.
- **Multi-capture corpora** per device (varying payloads + timestamps) to test cadence (§7a) and differential bitfield analysis (§7b).
Keep expected outputs alongside each fixture (golden files) so tests assert exact classifier/cadence/field-map results.

### 1.3 Log instrumentation convention
Instrument every target so its behavior is legible as **text**, since text is the cheapest thing to assert on:
- Structured, greppable log lines at scene/page transitions, capture events, classifier decisions, TX actions, and errors.
- A stable prefix + key=value form (e.g. `SC scene=review action=label device=... match=... conf=...`) so the harness can assert on them without brittle parsing.
- Zero uses `FURI_LOG_I/D`; Esp uses `ESP_LOG`/`Serial`; Pi uses `logging`. Same discipline everywhere.

---

## 2. SubCensusZero (Flipper Zero)

### 2.1 Build & deploy
- Build with `ufbt` (no device needed to compile). Deploy with `ufbt launch` over USB, **or** — if USB is unavailable — copy the `.fap` onto the microSD (`/apps/Sub-GHz/`) via a card reader / the Flipper mobile app over BLE; load from the on-device App Loader.
- Pin the SDK channel to the firmware actually installed (release/rc/dev or the custom-firmware SDK).

### 2.2 Observe — serial RPC harness (`tools/debug/flipper_*`)
The Flipper exposes an **RPC layer over serial (protobuf)** — the same interface qFlipper and lab.flipper.net use. A host-side Python helper (pyserial + the Flipper protobuf definitions, or an existing binding) gives the agent eyes and hands:
- **Screenshot:** the `Gui` screen-stream RPC pushes the 128×64 framebuffer (1024-byte 1-bit buffer). Decode → **PNG** (agent can view it natively) and/or an **ASCII render** for cheap text-only assertions ("is the cursor on row 3?").
- **Input:** the input RPC injects virtual button events (Up/Down/Left/Right/Ok/Back, short/long) to navigate.
- **Logs:** capture `FURI_LOG` output on the same serial link (also reachable via `ufbt cli`).
- **Loop primitive:** `screenshot → assert → send input → screenshot` — e.g. "open Review, confirm the label picker rendered, press Down×2, screenshot, assert selection moved."

Build this helper **first** — it's pure host-side Python, testable without the FAP, and every later UI test rides on it.

### 2.3 Host logic tests
Compile `shared/core/` natively and run the unit suite (§1.1/§1.2). This covers the bulk of the app with zero hardware.

### 2.4 Options & limits
- **flippulator** (community, Linux + SDL2) can compile-and-run the FAP UI on desktop for quick scene iteration — approximate, no radio; useful but not required given the framebuffer-over-serial path.
- Screen stream is **~sub-10 fps, monochrome** — fine for "right screen / cursor position," not for animation timing.
- RPC/protobuf details can shift on custom firmware — **pin the helper to your firmware**.
- **No radio:** RSSI/capture/TX only verifiable with real airtime.

### 2.5 Claude Code loop
build (`ufbt`) → flash/`launch` (or SD-card) → drive via serial RPC (screenshot + input) → read `FURI_LOG` → assert (image + text + golden fixtures) → edit → repeat. Radio behavior handed to the human for a real-signal pass.

---

## 3. SubCensusEsp (ESP32 + CC1101)

### 3.1 Build & deploy
- PlatformIO (`board = esp32dev`); flash over USB, or **OTA** once WiFi is up (so the agent can re-flash without touching the board).
- Serial monitor for `ESP_LOG` output.

### 3.2 Observe — it's headless, so drive the web UI
No screen to screenshot; the UI *is* a web interface, which is easier to automate:
- **HTTP/WebSocket driver:** hit the node's endpoints and WebSocket live feed directly (Python `requests`/`websocket-client`), or drive the rendered page with **Playwright** (real DOM assertions + browser screenshots of the served UI).
- Assert on served JSON (catalog, occupancy, watchlist, candidates) and on the live WebSocket capture stream.
- Serial log stream in parallel for firmware-level events.

### 3.3 Host logic tests
The **shared `core/`** (§1.1) compiles for the ESP too; run the same native unit suite. PlatformIO's `native` env or plain `gcc` on the core modules.

### 3.4 Limits
No CC1101 emulation — feed the decode/classify path from `.sub`/fixture injection (a debug endpoint or a compiled-in fixture loader) rather than live RF. Live capture/replay needs real airtime.

### 3.5 Claude Code loop
build (PlatformIO) → flash (USB/OTA) → drive web UI (HTTP/WS/Playwright) + read serial → assert → edit → repeat. Fixture-inject the RF path; real radio to the human.

---

## 4. SubCensusPi (RTL-SDR / Raspberry Pi)

The **strongest autonomous target** — it's pure Python on Linux, so most of it runs in CI (or in this sandbox) with no hardware at all.

### 4.1 Test surface
- **`pytest`** over the collector, catalog logic, cadence/field-map derivation, and the SQLite layer.
- **Radio-free capture path:** run `rtl_433 -r <fixture.cu8>` (or feed recorded `rtl_433` JSON straight to the collector) so the full decode → device roll-up → SQLite path is deterministic and needs no dongle.
- **Web dashboard:** drive FastAPI with `httpx` (API assertions) and/or **Playwright** (page + screenshots).
- **DB assertions:** query the SQLite directly for expected devices/events/cadence/place scoping.

### 4.2 Limits
Only *live* dongle behavior (real gain/ppm/reception) needs hardware; everything downstream of a captured sample is fixture-testable.

### 4.3 Claude Code loop
Fully local/CI: run collector against fixtures → `pytest` + API/Playwright assertions → edit → repeat. This target can go green end-to-end without any device.

---

## 5. Repo layout (harness & tests)
```
subcensus/
  shared/core/                 # shared C logic (Zero+Esp), host-compilable
  tools/debug/
    flipper_screen.py          # serial RPC: framebuffer → PNG/ASCII
    flipper_drive.py           # serial RPC: inject input events
    flipper_serial.py          # FURI_LOG capture
  test/
    fixtures/                  # .sub, .cu8, rtl_433 JSON, synthetic frames + golden outputs
    core/                      # native unit tests for shared/core
  zero/test/  esp/test/  pi/test/   # per-target harness tests
```

## 6. Build order (recommended)
1. **Fixtures + `shared/core/` host tests** — the deterministic base; green with no hardware, and it de-risks the hardest logic first.
2. **`tools/debug/flipper_*` serial helper** — screenshot + input + log; the harness every Zero UI test rides on.
3. **Pi test surface** — fixtures through the collector + FastAPI/Playwright; the target that can go fully green in CI.
4. **Esp web-UI driver** + native core tests.
5. **On-device UI loops** (Zero framebuffer, Esp web) against real firmware.
6. **RF validation passes** (human + antenna) — real capture/replay, the only step automation can't own.

## 7. The RF boundary (applies to all targets)
No tool emulates the CC1101 or RTL-SDR. Automation proves the *processing* (decode, classify, cadence, field-map, log, UI) via fixtures; a human proves the *physics* (a real signal was received / a real device reacted to a replay). Keep test suites honest about which side of that line each assertion is on.
