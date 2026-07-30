# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

Open-source sub-GHz RF pentesting device: a WEMOS D1 Mini (ESP8266) driving a CC1101 radio module, plus the PCB/hardware design files to build it. The firmware exposes a serial CLI (YardStick-One-like) for scanning, transmitting, jamming, brute-forcing, and record/replay of sub-GHz signals. It also works alongside a Flipper Zero. This is a hardware project, not a conventionally "built" software repo.

## Repository layout

- `cc1101-tool-esp8266/cc1101-tool-esp8266.ino` — the active firmware for this board (WEMOS D1 Mini / ESP8266). This is the file to edit for firmware changes.
- `original_files/` — upstream Arduino sketch variants for other MCUs (arduino-nano, esp32, esp32-wroom, esp32c3/XIAO, rp2040, pro-micro, and ESP8266 WiFi-client / WiFi-AP builds), plus datasheets, pinout diagrams, and the `SmartRC-CC1101-Driver-Lib-master.zip` driver. Reference/porting material — not compiled by default.
- `hardware/` — PCB design output: `PCB_cypher_cc1101_jammer.svg` and `GERBER_*.zip` for fabrication.
- `device_img/`, `Esp8266_CC1101.png` — build/product photos and wiring image.
- `README.md` — full CLI command reference, per-board pin maps, and dated changelog. `BEGINNER.md` — intro + legal/safety notes.

## Building & flashing (Arduino IDE)

There is no CLI build system, Makefile, or test suite. Firmware is compiled and flashed via the Arduino IDE:

1. Install the **SmartRC-CC1101-Driver-Lib** (ELECHOUSE fork by Little_S@tan): https://github.com/LSatan/SmartRC-CC1101-Driver-Lib — either the GitHub zip or the copy in `original_files/`.
2. Add ESP8266 board support and select the WEMOS D1 Mini board.
3. Recommended: set CPU frequency to **160 MHz** (Tools menu), especially for WiFi builds.
4. Open `cc1101-tool-esp8266/cc1101-tool-esp8266.ino`, compile, and upload.
5. Connect at **115200 baud** over USB serial (or telnet on WiFi builds). Send `help` for the command list. Set the terminal newline to **CR only** — extra LF characters can break commands like `rxraw`.

A "Low Memory" warning during compilation is expected and harmless.

## Firmware architecture

Single-translation-unit Arduino sketch. Key structure in `cc1101-tool-esp8266.ino`:

- **Pin map & sizing constants** at the top (`sck/miso/mosi/ss/gdo0/gdo2`, `RECORDINGBUFFERSIZE`, `EPROMSIZE`, `CCBUFFERSIZE`, `BUF_LENGTH`). Porting to another MCU means changing these — the README documents the correct values per board (ESP32, XIAO C3, Nano, RP2040, Pro Micro). ESP8266 uses `EPROMSIZE 4096` (flash-simulated EEPROM).
- **Global mode flags** (`receivingmode`, `jammingmode`, `recordingmode`, `chatmode`) act as a small state machine; `loop()` branches on them. The `x` command clears activity and `init` re-runs `cc1101initialize()` with defaults.
- **`exec(char *cmdline)`** (~line 155) is the command dispatcher: a long `strcmp_P(command, PSTR("..."))` if/else chain. **To add or change a CLI command, edit this chain** and mirror the change in the `help` text block (~line 173) and in `README.md`'s command list.
- **`cc1101initialize()`** sets default radio parameters (433.92 MHz etc.) via the ELECHOUSE driver.
- **Buffers**: `ccsendingbuffer`/`ccreceivingbuffer` (packet mode, `CCBUFFERSIZE`) and `bigrecordingbuffer` (`RECORDINGBUFFERSIZE`) for RAW record/replay. `save`/`load` persist the recording buffer to EEPROM.
- **`asciitohex()` / `hextoascii()`** convert between the hex the CLI accepts and raw bytes.

RAW record/replay (`rxraw`/`recraw`/`playraw`/`showraw`/`showbit`) uses a bit order chosen to be interoperable with **Universal Radio Hacker** (https://github.com/jopohl/urh). After any RAW-mode operation, run `init` to reset the chip.

### ESP8266-specific constraints

The ESP8266 is single-core and heavily loaded by the WiFi/TCP-IP stack, so timing is fragile:
- A **software watchdog** is armed in `setup()` (`ESP.wdtEnable`) and must be fed (`ESP.wdtFeed()`) in `loop()`. Long-running or tight-timing code can trigger watchdog resets.
- Always pass the `<microseconds>` argument to `rxraw`/`playraw`; omitting it can cause a stack overflow / reset on WiFi-enabled builds.
- Default packet-mode data rate is 9.6 kBaud (raised from 1.2 kBaud specifically to avoid watchdog resets).

## Radio behavior notes

- CC1101 frequency bands: 300–348 MHz, 387–464 MHz, 779–928 MHz. Default 433.92 MHz.
- The CC1101 requires **3.3 V VCC and 3.3 V TTL logic**. 5 V boards (e.g. Arduino Nano) need a TXS0108E level shifter or the chip can be damaged — relevant when porting/wiring, not when editing ESP8266 firmware.
- Known issue (upstream): packet-mode `rx` can misbehave after many large frames — suspected SmartRC memory leak.

## Legal / safety framing

This is a security-research and education tool. RF jamming and transmitting on unauthorized frequencies is illegal in many jurisdictions. When adding or documenting features (especially `jam`, `brute`, `tx`, replay), preserve the existing responsible-use framing in `README.md`/`BEGINNER.md` rather than removing it.
