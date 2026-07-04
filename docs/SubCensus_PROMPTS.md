# SubCensus — Claude Code Kickoff Prompts

Paste-ready `/goal` prompts for building SubCensus with Claude Code, one session per target.

## Setup (once)
- Create a folder `SubCensus/` and put **all five** specs in it (the System + Debug docs cross-reference every target, so Claude Code wants the full set even when building just one):
  ```
  SubCensus/
    SubCensus_System.md
    SubCensus_Debug.md
    SubCensusZero_Spec.md
    SubCensusPi_Spec.md
    SubCensusEsp_Spec.md
  ```
- Run Claude Code **inside that folder**: `claude --dangerously-skip-permissions` (verify the flag with `claude --help` if it's changed).
- For the private GitHub repo, have `gh` authenticated first (`gh auth login`). Without it, Claude Code will make the local repo and you create the remote yourself.
- **Run the sessions in order.** Session 1 (Zero) builds the shared layer (`shared/taxonomy.yaml`, `shared/schema/`, `shared/core/`, the signatures/place/catalog contract, and the `tools/debug/` + `test/` harness) that Sessions 2 and 3 reuse.

---

## Session 1 — SubCensusZero (Flipper) + shared layer

```
Read all five .md specs in this folder before writing any code.
SubCensus_System.md is the AUTHORITATIVE shared contract — everything must
conform to it. SubCensus_Debug.md defines the test harness and build order.
SubCensusZero_Spec.md is the target for this session. SubCensusPi_Spec.md and
SubCensusEsp_Spec.md are CONTEXT ONLY — do not implement them this session.

Scope this session: implement the shared layer the Zero depends on (repo
scaffolding, shared/taxonomy.yaml + codegen, shared/schema/, shared/core/
logic library, and the tools/debug/ + test/ harness) and the SubCensusZero
Flipper FAP.

Workflow:
1. git init here, then create a PRIVATE GitHub repo named SubCensus
   (gh repo create SubCensus --private --source=. if gh is authenticated;
   if not, set up the local repo and tell me to make the remote).
   Also write a CLAUDE.md capturing the invariants: System.md is
   authoritative; the RF boundary (no CC1101 emulation — fixtures prove
   processing, hardware proves physics); commit per milestone; pin the
   ufbt SDK to the official release channel unless I say otherwise.
2. Work MILESTONE BY MILESTONE per SubCensusZero_Spec.md §9 (start at
   Phase 0), but honor the build order in SubCensus_Debug.md §6:
   front-load host-testable work — fixtures + shared/core unit tests +
   the serial harness — so we progress without the Flipper attached.
3. After each milestone: run the relevant tests, and use ufbt to
   compile-check the FAP (pip install ufbt; ufbt then ufbt lint), then
   COMMIT with a message referencing the milestone
   (e.g. "feat(zero): M2 camp capture engine").
4. I do NOT have the Flipper reliably connected. For hardware-dependent
   milestones (live RSSI/capture/TX): implement fully, cover everything
   testable with fixtures + compile-check, mark on-device validation as
   TODO, and continue — do not block.
5. PAUSE and report at each milestone boundary. ASK me before deviating
   from the specs or deciding anything the specs don't cover — don't
   silently guess. (--dangerously-skip-permissions means you won't prompt
   for file/command permission, but still surface real design questions.)

Start by reading the specs and giving me a short plan plus your Phase 0
findings (prior-art study + firmware/SDK pin) BEFORE writing code.
```

---

## Session 2 — SubCensusPi (RTL-SDR / Raspberry Pi)

Run later, in the same `SubCensus/` folder/repo. This is the strongest autonomous target — it can go green with **no hardware**.

```
You are working in the existing SubCensus monorepo (created in a prior
session). Read all five .md specs before writing any code.
SubCensus_System.md is the AUTHORITATIVE shared contract. SubCensus_Debug.md
defines the test harness / build order. SubCensusPi_Spec.md is this session's
target. SubCensusZero_Spec.md and SubCensusEsp_Spec.md are CONTEXT ONLY.

Reuse, do not recreate: shared/taxonomy.yaml, shared/schema/, and the
signatures/place/catalog contract already exist from the Zero session. Do
NOT modify the shared contract or re-init git — work in the pi/ subtree
against the existing shared/ definitions. The Pi is Python, so reimplement
the shared algorithms (feature vector, cadence, k-NN, differential/field-map
per System §6-7b) in Python and verify them against the SAME test/fixtures
golden files the C core uses, so both tools stay consistent.

This target can go green with NO hardware:
- Use rtl_433 -r on recorded .cu8/.ook fixtures (and recorded rtl_433 JSON)
  to drive the full decode -> catalog -> SQLite path (Debug §4).
- pytest for logic/DB; httpx + Playwright for the FastAPI dashboard.
- Mock or run a local mosquitto for MQTT / Home Assistant discovery tests.
I likely do NOT have an RTL-SDR dongle connected; only LIVE dongle behavior
(gain/ppm/real reception) needs hardware — implement it, cover everything
downstream with fixtures, mark live capture as TODO, do not block.

Workflow:
1. Confirm the repo + shared/ layer; add a pi/CLAUDE.md note if helpful.
2. Work MILESTONE BY MILESTONE per SubCensusPi_Spec.md §11, honoring the
   fixtures-first build order in SubCensus_Debug.md §6/§4.
3. After each milestone: run pytest + API/Playwright assertions; COMMIT with
   a milestone-referenced message (e.g. "feat(pi): M1 collector->SQLite").
4. PAUSE and report at each milestone boundary; ASK before deviating from
   the specs or deciding anything they don't cover.

Start by reading the specs, confirming the existing shared/ contract, and
giving me a short plan BEFORE writing code.
```

---

## Session 3 — SubCensusEsp (ESP32 + CC1101)

Run later, in the same `SubCensus/` folder/repo. Reuses `shared/core/` directly (the ESP shares the Zero's CC1101 capture model).

```
You are working in the existing SubCensus monorepo (created in a prior
session). Read all five .md specs before writing any code.
SubCensus_System.md is AUTHORITATIVE. SubCensus_Debug.md defines the test
harness / build order. SubCensusEsp_Spec.md is this session's target.
SubCensusZero_Spec.md and SubCensusPi_Spec.md are CONTEXT ONLY.

Reuse, do not recreate: shared/taxonomy.yaml, shared/schema/, and especially
shared/core/ — the ESP shares the CC1101 capture model with the Zero, so the
C logic library (feature vector, cadence, k-NN, .sub/pulse en-decode, CRC,
differential/field-map) already exists and COMPILES INTO THE ESP FIRMWARE.
Do NOT reimplement it, modify the shared contract, or re-init git — work in
the esp/ subtree and link shared/core/.

Scope: the headless ESP32 + CC1101 node (PlatformIO), web UI over WiFi,
LittleFS default / optional-SD storage, MQTT/HA, brain sync, OTA.

Front-load the hardware-free work (I may not have the ESP32 reliably
connected):
- Reuse the shared/core native unit tests (already green from Session 1).
- Drive the served web UI headlessly with HTTP/WebSocket + Playwright
  (Debug §3); assert on served JSON + the live WebSocket feed.
- Fixture-inject the decode/classify path (.sub/fixtures) instead of live RF.
- Flash over USB when available, else OTA once WiFi is up.
Only LIVE radio (RSSI/capture/TX) needs hardware — implement fully, cover the
rest with fixtures + web-UI tests, mark on-device validation TODO, don't block.

Workflow:
1. Confirm repo + shared/ (esp compiles shared/core); add esp/CLAUDE.md note.
2. Work MILESTONE BY MILESTONE per SubCensusEsp_Spec.md §8, honoring
   SubCensus_Debug.md §6 build order (core tests -> web-UI driver -> on-device).
3. After each milestone: run tests + a PlatformIO build (pio run) as a
   compile-check; COMMIT with a milestone-referenced message
   (e.g. "feat(esp): M2 RMT capture + camp").
4. PAUSE and report at each milestone boundary; ASK before deviating from the
   specs or deciding anything they don't cover.

Start by reading the specs, confirming shared/core links into the ESP build,
and giving me a short plan BEFORE writing code.
```

---

## Notes
- **Phase 0 is a gate** in the Zero spec — it studies prior art (ProtoView / Read RAW / Frequency Analyzer) and pins the firmware/SDK before coding. Let it run first.
- **Private repo needs `gh` authenticated.** Local `git init` + commits work regardless.
- **Order matters:** the shared layer from Session 1 is a dependency for Sessions 2 and 3. Cross-tool parity is enforced by running the Pi's Python logic against the same `test/fixtures/` golden files as the C `shared/core/`.
- **The RF wall is the same everywhere:** automation proves the processing (via fixtures); a human with an antenna proves the physics.
