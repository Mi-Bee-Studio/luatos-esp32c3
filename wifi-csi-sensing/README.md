# ESP32-C3 Sense Node (WiFi CSI sensing terminal)

[中文文档](README.zh.md) | [English](README.md)

![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)
![ESP-IDF](https://img.shields.io/badge/ESP-IDF-v6.0-green.svg)
![LVGL](https://img.shields.io/badge/LVGL-v9-orange.svg)

**v2.0 — repositioned.** This board is now a dedicated WiFi-CSI sensing terminal
for **homepulse** (the PC-side sensing engine, see
[Mi-Bee-Studio](https://github.com/Mi-Bee-Studio)), connected through the
serialtap USB proxy. The old info-display features (clock, weather, timer,
PC monitor and the companion desktop tool) were removed.

**v2.1 — baseline capability: on-board web OTA.** Firmware slimming freed enough
of the 2MB flash for **dual OTA slots** (960K × 2, app offset stays 0x10000):
the settings page gains firmware upload (invalid images never switch slots —
the running firmware is untouched) and reboot; the old "2MB can't fit dual
slots, USB-only flashing" conclusion is obsolete. Slimming trade-offs
(reversible for debug builds, see comments in sdkconfig.defaults): log level
compiled to ERROR, WPA3-OWE disabled (**SAE kept** — 2026-09-27 hardware lesson:
the home router is WPA3-SAE-only; disabling SAE loses the link),
LVGL trimmed of unused widgets / flex / grid / examples / the 8/14/16pt fonts
(bpm label moved 14→12pt), plus `-Oz` size optimization.
Partition-table change: nvs 24K→16K (existing data kept) + otadata added;
**the first upgrade flash must include the partition table**.

> **User manual: [docs/user-guide.md](docs/user-guide.md)** —
> pages, buttons, calibration, WiFi provisioning, troubleshooting.

## What it does

```
board CSI ──#S1 lines──> USB ──serialtap proxy──> homepulse DSP ──sense_status JSON──> LCD mirror
```

- **CSI capture**: after WiFi association, 16 selected subcarriers (I/Q) are
  extracted per packet and streamed as compact `#S1 seq t_ms rssi hex64` lines
  over USB serial (rate-limited, optional gateway-ping stimulus for stable CSI).
- **Task watchdog (2026-09-24)**: the LVGL tick task feeds the task watchdog
  every 5 ms tick; a hung render loop panics and reboots the node (unattended
  self-recovery). Web OTA is not available on this board — 2MB flash cannot
  hold two 1.3MB slots; firmware updates go through serialtap proxy flash.
- **LCD mirror (160×80)**: two pages.
  - **Sense** (default): big PRESENT/ABSENT/CALIBRATING/PAUSED, breathing rate +
    quality, motion bar + class.
  - **Node**: uptime, heap, WiFi RSSI/SSID/IP, firmware version, CSI counters.
- **Local fallback**: without a PC the board runs a coarse on-device presence
  estimator, and the UI says so (`BR -- local mode`).

## Buttons

| Key | Action |
|-----|--------|
| LEFT / RIGHT | Switch Sense ↔ Node page |
| UP | **Pause / resume `#S1` streaming** (global). Sense page shows `* PAUSED` while paused; the stimulus ping stops too; resume reuses the last `sense_start` parameters |
| DOWN | Write a one-line **diagnostic snapshot** to the serial log (status + CSI counters — visible in the serialtap log trail, for field troubleshooting) |
| CENTER (Sense page) | **Re-calibrate**: reset the local baseline and ask the PC engine to re-learn (30 s, room must be empty) |
| CENTER (Node page) | Diagnostic snapshot (same as DOWN) |

## On-board web (`http://<board-ip>/`)

Once WiFi is associated the board serves its own settings page
(`app_web.c`, esp_http_server :80, no auth — home-LAN usage). The IP is on
the LCD **Node page**. Open it from a phone on the same WiFi to control the
node **from outside the room** — resolving the "calibration needs an empty
room but every trigger needs a person present" catch-22:

- **Status**: presence / motion / breathing / rate / RSSI / WiFi+IP, 2s refresh
- **Calibrate**: immediate or **arm-and-leave (60 s countdown, cancellable)** —
  same semantics as the homepulse panel
- **WiFi provisioning**: scan list + password connect (re-network without USB)

REST: `GET /api/status` · `POST /api/calibrate {"delay_s":N|0,"cancel":true}` ·
`POST /api/wifi/scan` · `POST /api/wifi/connect {"ssid","pass"}`.

## Firmware layout

| File | Role |
|------|------|
| `main/app_sense.c` | CSI callback → queue → `#S1` stream task + local estimator + protocol commands |
| `main/ui/ui_common.h` | Layout system: `UI_LH_*` real line heights + `UI_ROW_ASSERT` compile-time overflow checks |
| `main/ui/ui_sense.c` / `ui_node.c` | The two pages |
| `main/app_wifi.c` | STA + NVS credentials + `wifi_scan`/`wifi_connect` provisioning |
| `main/app_protocol.c` | JSON line protocol over USB serial (cJSON) |

## Protocol (USB serial, JSON lines)

`hello` · `ui_page{page:sense|node}` · `wifi_scan` · `wifi_connect{ssid,pass}` ·
`sense_start{max_hz,stimulus_hz}` · `sense_stop` · `sense_calibrate{delay_s?}` ·
`sense_status{...}` (PC push, no ack) · `sense_info`;
telemetry `#S1 seq t_ms rssi hex64` (session header `#S1-HELLO`).

The `#S1` format is a contract with `homepulse/internal/sense/parser.go` —
never change one side alone.

## Overflow-proof layout

The v1 UI overflowed the 80px screen because rows were placed with
"font size ≈ occupied height" guesses, while LVGL line heights are larger
(montserrat_12 → 15px). v2 rules (enforced in `ui_common.h`):

1. Row y/heights must use the `UI_LH_8/12/14/16/22` constants (real
   `.line_height` values from the LVGL font sources);
2. Every row declares `UI_ROW_ASSERT(name, y, lh)` — a screen overflow is now a
   **compile error**;
3. `ui_row_label()` re-checks at runtime and refuses + logs out-of-bounds rows;
4. Pages build on the shared `ui_content_root()` (160×65 under a 15px title bar).

## Build & flash

```bash
idf.py build

# proxy flash through serialtap (pauses collector, runs esptool, resumes;
# device regex depends on your serialtap naming — example from the live setup)
serialtap flash esp32s3-jtag build/bootloader/bootloader.bin@0x0 \
  build/partition_table/partition-table.bin@0x8000 \
  build/wifi_csi_sensing.bin@0x10000
```

WiFi provisioning: send `{"cmd":"wifi_connect","ssid":"...","pass":"..."}\n`
through the serialtap proxy endpoint (or any serial terminal).

Build notes: `components/esp_lcd_st7735/` is a **vendored** copy of
`waveshare/esp_lcd_st7735` 1.0.1 with a one-line IDF v6.0 API patch
(`rgb_endian` → `data_endian`; the upstream field was renamed). Do not delete
it and re-pull — the managed version does not compile under IDF v6.0.

## Hardware

| Component | Specification |
|----------|---------------|
| MCU | ESP32-C3 (RISC-V) |
| Display | ST7735S 160×80 LCD (LuatOS Air101 LCD) |
| Buttons | 5x tactile switches (LEFT=5 UP=8 CENTER=4 DOWN=13 RIGHT=9) |
| Interface | USB-Serial-JTAG |

LCD pins: SCLK=2 MOSI=3 DC=6 CS=7 RST=10 (panel drive gap 1/26 — the tuning
that made this panel stable; keep it if you re-create the init).

## License

MIT
