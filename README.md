# Luatos ESP32-C3 + Air101 LCD (Sense Node Mainboard)

[中文文档](README.zh.md) | [English](README.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/luatos-esp32c3-air101lcd/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/luatos-esp32c3-air101lcd/actions/workflows/build.yml)

A board under the board-centric repo convention. **This repo is organized with the board as root:**

```
luatos-esp32c3-air101lcd/
├── README.md          # this file: all hardware info for this board
└── <project>/         # one directory per project built on this board (named by capability)
    ├── CMakeLists.txt / main/ / sdkconfig.defaults / main/idf_component.yml
    └── README.md      # project description + build/flash commands
```

Key points of the convention:

- **Board directory name** = board name (kebab-case); the root README covers hardware
  only, no business content;
- **Each project directory builds standalone**: it carries the complete ESP-IDF project
  trio (top-level CMakeLists, `main/`, `sdkconfig.defaults`); `cd <project> && idf.py build`
  produces the firmware;
- Projects share no code; when commonality is needed, copy first, and only consider
  extracting a shared component once things stabilize.

### Firmware baseline norms (mandatory fleet-wide)

Two baselines are mandatory for every MiBee firmware repo, and **every project** inside a
board repo must satisfy them:

1. **Watchdog: mandatory.** No naked main loops — tasks either subscribe to the ESP-IDF
   task watchdog (TWDT) and feed it periodically, or (on RP2040) enable the hardware
   watchdog.
2. **Web/API firmware upgrade (OTA): mandatory where the hardware allows.** This board
   has WiFi and its flash layout already reserves dual OTA slots (960 KB × 2), so every
   project ships a web upgrade path; wired flashing (serialtap/esptool) is the recovery
   path, not a substitute.

| Project | Watchdog | Web/API OTA |
|---------|----------|-------------|
| wifi-csi-sensing | ✅ per-task TWDT (`esp_task_wdt_add`/`reset`) | ✅ dual OTA slots + onboard web flashing |
| blackbox | ✅ per-task TWDT | ✅ dual OTA slots + onboard web flashing |

---

## Board Overview

| Item | Value |
|------|-------|
| Module/Chip | ESP32-C3 — RISC-V single core, rev v0.4 (verified on hardware) |
| Flash | 2MB, **onboard external, wired in DIO mode** (uses IO14–17 only, not broken out; frees IO12/13) |
| Wireless | 2.4GHz WiFi b/g/n + Bluetooth 5 (LE); **CSI enabled** (`CONFIG_ESP_WIFI_CSI_ENABLED`) |
| USB | **Native USB-Serial-JTAG, no USB-UART bridge chip** (console and flashing share one port; USB D-/D+ = IO18/19) |
| Display | Luat Air101 LCD: ST7735S 0.96″ 160×80, 4-wire SPI (SCLK/MOSI/DC/CS + RST) |
| Buttons | Onboard RST and BOOT (=IO9, hold during power-up for download mode); 5-way key LEFT/UP/CENTER/DOWN/RIGHT (active-low) |
| Onboard LEDs | D4 = IO12, D5 = IO13 (unused by firmware; IO13 also carries the DOWN key) |
| Dimensions/Breakout | 21 × 51 mm; castellated stamp holes along both edges, 2×16 = 32 pins |
| Partition table | Custom dual OTA slots 960K × 2 (web flashing baseline; both sub-projects) |

## Pinout Diagram (USB-C pointing up, front/component-side view; official LuatOS antenna-up pin numbers in parentheses)

> The fleet-wide standard orientation is **USB-C pointing up** (see `esp32-s3-zero` for the
> reference diagram). The official LuatOS pinout is antenna-up; this figure is that view
> rotated 180°, with the official pin numbers annotated per pad for direct cross-checking.

```
                 ┌─ USB-C ─┐
  GND(32) ◎     │          │     ◎ 5V(16)
   5V(31) ◎     │          │     ◎ PWB(15)
  IO9(30) ◎     │  CORE-   │     ◎ GND(14)   ← IO9 = key RIGHT = BOOT(IO9)
  IO8(29) ◎     │  ESP32   │     ◎ 3V3(13)   ← IO8 = key UP
  IO4(28) ◎     │    C3    │     ◎ RST(12)   ← IO4 = key CENTER
  IO5(27) ◎     │  21×51mm │     ◎ NC(11)    ← IO5 = key LEFT
  3V3(26) ◎     │          │     ◎ IO13(10)  ← IO13 = key DOWN + LED D5
  GND(25) ◎     │          │     ◎ U0TX(09)  ← U0TX = IO21
 IO11(24) ◎     │          │     ◎ U0RX(08)  ← U0RX = IO20; IO11 = VDD_SPI (burn eFuse to use as GPIO)
   IO7(23) ◎    │  [BOOT]  │     ◎ GND(07)   ← IO7 = LCD CS
   IO6(22) ◎    │   [RST]  │     ◎ IO19(06)  ← IO6 = LCD DC; IO19 = USB D−
  IO10(21) ◎    │          │     ◎ IO18(05)  ← IO10 = LCD RST; IO18 = USB D+
   IO3(20) ◎    │          │     ◎ IO12(04)  ← IO3 = LCD MOSI; IO12 = LED D4
   IO2(19) ◎    │          │     ◎ IO1(03)   ← IO2 = LCD SCLK
  3V3(18) ◎     │          │     ◎ IO0(02)   ← free (ADC0/ADC1, UART1 alt)
  GND(17) ◎     │          │     ◎ GND(01)
                └──────────┘
                ~ 2.4G antenna ~
      left row (official 32→17)  right row (official 16→01)
```

Key points:

- **Left row** (downward from the USB end, official 32→17): `GND, 5V, IO9, IO8, IO4, IO5, 3V3, GND, IO11, IO7, IO6, IO10, IO3, IO2, 3V3, GND`;
- **Right row** (downward from the USB end, official 16→01): `5V, PWB, GND, 3V3, RST, NC, IO13, U0TX, U0RX, GND, IO19, IO18, IO12, IO1, IO0, GND`;
- **The LCD and the 5-way key live almost entirely on the left row** (the only exception is DOWN on the right row): SCLK=IO2 · MOSI=IO3 · RST=IO10 · DC=IO6 · CS=IO7; keys CENTER=IO4 · LEFT=IO5 · UP=IO8 · RIGHT=IO9 · DOWN=IO13;
- **U0RX/U0TX = IO20/IO21** (UART0 broken out; the console goes over USB-Serial-JTAG and doesn't use them); **IO18/19 = USB D-/D+** — don't repurpose;
- **IO14–17 are not broken out**: taken by the onboard external flash (the DIO wiring uses only those 4 pins, which is why IO12/13 are free);
- **IO12 = onboard LED D4, IO13 = onboard LED D5** (IO13 doubles as the 5-way DOWN key, pressed = grounded; check the LED wiring before repurposing either);
- **IO11** defaults to VDD_SPI (flash power pin); to use it as GPIO you must burn the one-time `VDD_SPI_AS_GPIO` eFuse (this board's flash supply is fixed at 3.3V, so burning is allowed);
- **PWB**: 3.3V output enable (drive high to conduct) for powering peripherals; IO0/IO1 are free (ADC0/ADC1, also UART1 alternate functions).

## Actual Pin Usage (firmware source is authoritative: `wifi-csi-sensing/main/app_lcd.c`, `app_button.c`)

### LCD (ST7735S, SPI)

| Signal | GPIO |
|--------|------|
| SCLK | 2 |
| MOSI | 3 |
| DC   | 6 |
| CS   | 7 |
| RST  | 10 |

Driver components: `waveshare/esp_lcd_st7735 ^1.0.1` + LVGL `^9.5.0`.

### 5-way key (all five enabled, GPIO active-low)

| Key | GPIO | Action |
|-----|------|--------|
| LEFT   | 5  | Switch to the previous page |
| RIGHT  | 9  | Switch to the next page |
| UP     | 8  | Pause/resume CSI streaming (global; the Sense page shows `* PAUSED`; resume reuses the original sampling parameters) |
| DOWN   | 13 | Write a one-line diagnostic snapshot to the serial log (status + CSI counters, visible in the serialtap log trail) |
| CENTER | 4  | Sense page = re-calibrate; Node page = diagnostic snapshot |

See [`wifi-csi-sensing/README.md`](wifi-csi-sensing/README.md) and the
[user manual](wifi-csi-sensing/docs/user-guide.md) for details.

## Caveats

- **No USB-UART bridge**: the serial port is the C3's USB-Serial-JTAG
  (`CONFIG_USB_SERIAL_JTAG_ENABLED=y`), and the console runs on the same port.
  #S1 telemetry, JSON commands such as sense_start, and logs all share this one
  USB serial line.
- **The 5-way key sits on strapping pins**: RIGHT = IO9 = the onboard BOOT button —
  holding RIGHT during power-up/reset enters download mode; UP = IO8 — holding it
  acts as an external pull-down, which blocks firmware download — **don't hold UP
  while power-cycling, entering download, or flashing**.
- The C3 single core cannot run the full DSP: the board **only captures and streams**
  (CSI → `#S1` lines); spectrum analysis / presence decisioning happen PC-side in
  homepulse (the PC-side sensing platform), which pushes status JSON back for the
  LCD mirror.
- LVGL traps (`wifi-csi-sensing/main/ui/`): `set_text_fmt` with `%f` always crashes
  (use `ui_label_fmt` instead); cross-task page switches must go through the request
  flag — never call `lv_scr_load` directly.
- **The LCD component is vendored**: `wifi-csi-sensing/components/esp_lcd_st7735/` is
  waveshare 1.0.1 adapted to the IDF v6.0 API (`rgb_endian` → `data_endian`).
  The version pulled by the component manager does not compile under v6.0 — do not
  delete it or re-pull.

## Serialtap (middleware) integration points

- On USB it enumerates as USB-Serial-JTAG, with the same device-name family as the S3
  (verified: the `esp32c3` / `esp32s3-jtag` family); serialtap's discovery / naming /
  collection / pass-through / flashing chain works as-is;
- Firmware is ~1.4MB with flash time in the same ballpark; proxy flashing
  (yield port → esptool → resume capture) is verified to work;
- When sharing the endpoint with serialtap, the proxy's hold on the port makes
  `idf.py` flashing get rejected — pause the proxy first, or go through
  `serialtap flash`.

## Toolchain

**ESP-IDF v6.0** (what this board actually runs; `sdkconfig.defaults` is tuned for
v6.0 + C3). In Git Bash, `idf.py` is rejected by MSys detection — build via
PowerShell, or use the cached toolchain with direct ninja (see
[wifi-csi-sensing/AGENTS.md](wifi-csi-sensing/AGENTS.md) for the Git Bash /
direct-ninja build recipe and engineering conventions).

## Project Index

| Project | Description |
|---------|-------------|
| [wifi-csi-sensing](wifi-csi-sensing/README.md) | WiFi CSI sensing terminal: CSI capture + `#S1` stream + Sense/Node two-page LVGL UI — the board-side node of the homepulse sensing engine (v2.0) |
| [blackbox](blackbox/README.md) | Network probing terminal (Prometheus blackbox_exporter compatible): 8 prober types + web dashboard + metrics on :9090 — this board's port of esp32-blackbox |

## License

MIT — see [LICENSE](LICENSE) (each sub-project ships its own).
