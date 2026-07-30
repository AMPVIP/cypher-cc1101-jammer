# Design: ESP8266 CC1101 tool — AP web server (v2)

**Date:** 2026-07-30
**Target:** `cc1101-tool-esp8266/cc1101-tool-esp8266.ino` (WEMOS D1 Mini / ESP8266)
**Status:** approved design, pending implementation plan

## Goal

Add a browser-based control panel to the ESP8266 CC1101 tool. The board runs its own
WiFi access point and serves a single-page web UI that can drive the full command set —
including the long-running sweep/RAW commands — while the existing serial CLI keeps working.

"v2" means the long-running commands are refactored to be non-blocking (cooperatively
serviced in `loop()`), so the web server stays responsive and commands are controllable
from the browser rather than only stoppable by a serial keypress.

## Non-goals

- Station (join-your-router) mode. AP mode only. (Station remains available in the
  separate `original_files/cc1101-tool-esp8266-wifi.ino` variant.)
- Live server-push streaming (SSE/WebSocket). The UI polls a status endpoint instead.
- Authentication beyond the WPA2 AP password.
- Changing any RF behavior, command names, or command output strings.

## AP configuration (defaults, editable at top of sketch)

- `WiFi.softAP` in `WIFI_AP` mode, WPA2.
- SSID `cc1101`, password `cc1101`, static IP `192.168.1.100` (gateway `.1`, mask `/24`).
- Web UI at `http://192.168.1.100`, port 80.
- Recommend 160 MHz CPU (consistent with the existing WiFi variants).
- Serial CLI (115200 baud) runs concurrently with the web server.

## Architecture

### Reuse `exec()` via a redirectable output target
The core principle: the web layer routes into the **existing** command logic, not a
reimplementation. Today commands write directly to `Serial`. Introduce one indirection so
command output can be captured to a String for HTTP responses:

- A global `Print* out = &Serial;`. All **output** calls inside `exec()` and the command
  code (`Serial.print` / `println` / `write`) are routed through `out` (e.g. `out->print(...)`).
  This is a mechanical, scriptable substitution across the command bodies.
- **Input** stays on `Serial` (`Serial.available()` / `Serial.read()` are unchanged — the
  serial CLI reader and the serial-keypress stop check).
- `/cmd` handling: point `out` at a `String`-backed `Print` sink for the duration of the
  `exec()` call, capture the text, restore `out = &Serial`, and return the captured String
  in the HTTP response. Serial-initiated commands leave `out == &Serial`, so serial output
  is unchanged.
- All 48 commands are reused unchanged behind this single indirection. Output strings are
  byte-for-byte identical; only the destination pointer changes.

### HTTP API (ESP8266WebServer, port 80 — 3 endpoints)
- `GET /` → the single HTML page (stored in `PROGMEM`).
- `POST /cmd` with body `c=<cli string>` → runs the string through `exec()` with output
  captured; returns `text/plain`.
  - **Quick commands** (`setmhz`, `tx`, `show`, `save`, `load`, `flush`, `init`, all `setX`,
    `add`, `addraw`, `getrssi`, `showraw`, `showbit`) run synchronously and return their output.
  - **Long-running commands** (`scan`, `rxraw`, `jam`, `rx`, `rec`, `brute`) **start a mode**
    and return an ack immediately; progress is observed via `/status`.
  - `recraw`/`playraw` run to completion inside the handler (see timing constraint) and
    return when done; the UI shows a busy state meanwhile.
- `GET /status` → JSON: `{ mode, freqMHz, modulation, drate, ..., scanBestFreq, scanBestRssi,
  frames, bufferPos, uptimeMs }`. Polled by the page (~1.5 s).

Both the raw command box and the curated UI controls funnel through `/cmd` (the controls
just build the equivalent CLI string), so there is one code path to the radio.

### State-machine refactor (the v2 core)
Consolidate the existing separate mode flags (`receivingmode`, `jammingmode`,
`recordingmode`, `chatmode`) into a single `activeMode` enum:

`IDLE, RX, JAM, REC, CHAT, SCAN, SNIFF, BRUTE`

`loop()` services whichever mode is active, one slice per pass, and stops it on any of:
a web Stop (`/cmd` `x` or re-toggling the same action), a serial keypress (preserving
current serial behavior), or the `x` command.

Per-command v2 behavior:

| Command | v2 behavior | Chunk granularity |
|---|---|---|
| `rx` / `jam` / `rec` | already non-blocking; fold into `activeMode` | per `loop()` pass |
| `scan` | one frequency step per pass; `/status` exposes current best freq/rssi | one freq step |
| `rxraw` (sniff) | capture one buffer per pass; latest buffer viewable via `showraw`/`showbit` | one buffer fill |
| `brute` | a batch of codes per pass; `/status` exposes progress | a code batch |
| `recraw` | one-shot: wait-for-signal then capture the full buffer in one timing-coherent pass | whole capture |
| `playraw` | one-shot: replay the whole buffer in one timing-coherent pass | whole replay |

**RF-timing constraint (must be honored):** RAW record/replay is precise
`delayMicroseconds` bit-banging on GDO0 — the timing *is* the signal. `handleClient()`,
`yield()`, or mode-slicing must NOT be interleaved inside a RAW capture/replay inner loop.
Therefore `recraw`/`playraw` own the CPU for their duration (a few seconds at most); the
web server is briefly unresponsive during them, which is acceptable. All other long
commands are genuinely cooperative and keep the server responsive.

**Serial UX consequence (intended):** on serial too, `scan`/`rxraw`/`brute` will return to
the prompt immediately and run in the background (stop with any key or `x`) instead of
blocking the terminal — consistent with the web behavior and arguably nicer.

### `loop()` structure (v2)
```
loop():
  wdtFeed()
  server.handleClient()        // service one HTTP request if pending
  read+dispatch serial CLI     // unchanged, feeds exec()
  service activeMode one slice  // scan step / sniff buffer / brute batch / jam packet / rx+rec servicing
  yield()
```
`recraw`/`playraw` execute within the `/cmd` (or serial `exec`) call itself, not as a mode.

## UI design

Single self-contained page (all CSS/JS inline; no external assets — required, the ESP
serves everything). Style: **minimal monospace, light, calm "hacker" vibe.**

- **Type:** `ui-monospace, SFMono-Regular, Menlo, Consolas, monospace` throughout.
- **Palette (light):** paper `#f7f7f4`, ink `#222`, hairline dividers `#ddd`, one muted
  accent (soft green `#2e7d32`) for active states/links. No neon, generous whitespace,
  terse lowercase labels, a subtle blinking-cursor accent.
- **Sections (stacked):**
  1. `cc1101` header + live status line (mode · freq · uptime) from `/status`.
  2. **radio** — fields: freq, modulation (dropdown), drate, deviation, rxbw, PA → `apply`.
  3. **actions** — buttons reflecting running state: `sniff` `scan` `jam` `rx` `rec`
     (toggles), `rec-raw` `play-raw` `brute` (one-shot). Active toggles highlight in accent.
  4. **buffer** — view `frames` / `raw` / `bits`; `save` `load` `flush`; `tx` hex field → `send`.
  5. **console** — raw command input + scrollback output pane (shows `/cmd` responses and status).
- Page kept small to fit PROGMEM comfortably; responsive enough for a phone (AP use in the field).

## File structure

- Extend `cc1101-tool-esp8266/cc1101-tool-esp8266.ino`: WiFi/AP setup, `activeMode` enum +
  state, the output-capture sink, refactor of `scan`/`rxraw`/`brute` to non-blocking, and
  the `loop()` servicing + `server.handleClient()`.
- New tab `cc1101-tool-esp8266/webserver.ino` (Arduino concatenates `.ino` files in a sketch
  folder): the `PROGMEM` HTML page and the `/`, `/cmd`, `/status` handlers. Keeps the web
  layer isolated from the core radio code.

## Constraints & budget

- Flash/RAM: the serial-only build is 25% flash / 40% RAM; WiFi variants ~50% RAM. Adding
  WebServer + the HTML page is comfortably within budget.
- Keep the source ASCII + LF (the `.gitattributes` rule already enforces this).
- Explicit forward prototypes for any new functions (Arduino auto-prototype generator bug).

## Verification

- **Compile:** `arduino-cli compile --fqbn esp8266:esp8266:d1_mini` clean, with usage summary.
- **String preservation:** command output strings unchanged (diff literals vs current).
- **On-device (user):** join AP `cc1101`, load `http://192.168.1.100`; exercise apply/config,
  a `scan` (watch status update + stop), `sniff` then view buffer, `tx`, `save`/`load`,
  `rec-raw`/`play-raw` busy→done, and confirm the serial CLI still works concurrently.

## Risks

- Single-core timing tension between WiFi servicing and RF operations — mitigated by the
  cooperative mode-slicing and by letting RAW record/replay own the CPU briefly.
- The mode-flag → enum consolidation touches the `loop()` RX/record path; must preserve the
  existing receive/record/chat behavior exactly. Covered by compile + careful review.
- HTML page size vs PROGMEM — keep the page lean.
