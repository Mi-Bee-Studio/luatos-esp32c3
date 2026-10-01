# blink — Bare Core Board Baseline (Test Firmware)

> **Top-level project**: depends only on the LuatOS ESP32-C3 CORE board, **no
> Air101 LCD attached**. Isomorphic copy of
> [`esp32-s3-zero/blink`](../../esp32-s3-zero/blink) (copy-first convention).

## What it does

- Onboard LED **D4 (GPIO12)** toggles every second;
- **BOOT button (GPIO9)** held down keeps the LED on (interaction self-check);
- One heartbeat log line every 10 s (`uptime` / `heap`) for serialtap capture;
- Onboard **web maintenance page :80**: WiFi provisioning / firmware OTA / reboot
  (rescue SoftAP `blink-c3` / `12345678` → `192.168.4.1` when unprovisioned);
- **Watchdog**: ESP-IDF TWDT, 5 s timeout with panic (main loop feeds every 1 s)
  — mandatory baseline;
- **OTA**: custom partition table with **dual OTA slots 960 KB × 2** (hard 2 MB
  flash constraint, app starts at `0x10000`); the web page streams to the
  alternate slot → verify → switch → reboot.

## Build & Flash

ESP-IDF **v6.0** (local eim install: `C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1`;
CI uses the `espressif/idf:v6.0` container). `idf.py` refuses MSys shells —
build from PowerShell (drop `MSYSTEM` from the env if launched from Git Bash):

```powershell
cd blink
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

- Console runs on the native **USB-Serial-JTAG** (no USB-UART bridge on this board);
- The release assets include `flash_blink.sh/.bat` (bootloader + partition
  table + app in one go).

## Firmware baseline norms

| Baseline | Status |
|----------|--------|
| Watchdog | ✅ TWDT 5 s panic, main loop feeds |
| Web/API OTA | ✅ dual OTA slots + streaming `POST /ota` |

## Pins used

| Signal | GPIO | Notes |
|--------|------|-------|
| LED D4 | 12 | onboard LED (1 s toggle) |
| BOOT | 9 | onboard button; held at power-up enters download mode; used as input in firmware |
| USB D-/D+ | 18/19 | native USB — do not repurpose |

Board-level facts: root [README.md](../README.md).
