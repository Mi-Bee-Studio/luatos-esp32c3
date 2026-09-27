# blackbox — Network Probing Terminal (Prometheus blackbox_exporter compatible)

[中文文档](README.zh.md) | [English](README.md)

A standalone network-probing project on the Luatos ESP32-C3 board: HTTP/HTTPS/
TCP/TLS/DNS/ICMP/WebSocket probes over WiFi, configured via the web dashboard
or JSON, exporting metrics in standard Prometheus format — ready for scraping
by Prometheus / MiBeeSteward / Grafana.

The code originates from
[Mi-Bee-Studio/esp32-blackbox](https://github.com/Mi-Bee-Studio/esp32-blackbox)
(a dual-board project for the C3 SuperMini and C6 XIAO). This directory is the
**port for this board**; the probing core is byte-identical to upstream.

## Endpoints

| Port | Endpoint | Description |
|------|----------|-------------|
| 9090 | `/metrics` | Aggregated Prometheus metrics for all configured targets |
| 9090 | `/probe?target=X&module=Y[&port=P]` | On-demand probe (no pre-configured target needed) |
| 80 | `/` | Web dashboard (probe config / WiFi provisioning / reboot) |
| 80 | `POST /ota` | Firmware upload (**not available on this board**, see below) |

The full config JSON schema (modules / targets / scrape_interval) is documented
in the upstream [README](https://github.com/Mi-Bee-Studio/esp32-blackbox).

## Differences vs upstream (board adaptation)

| Item | Upstream (C3 SuperMini / C6 XIAO) | This board (Luatos ESP32-C3) |
|------|-----------------------------------|------------------------------|
| Flash | 4MB, dual OTA slots (1344K × 2) + SPIFFS | **2MB, dual OTA slots 960K × 2 + 64K SPIFFS** (firmware slimmed to fit: -Os + silent assertions + WPA3-OWE off, SAE kept — the router is WPA3-SAE-only; app 975K, 1% slot headroom) |
| Flashing | Web OTA or USB | **Web OTA available** (:80 dashboard / `POST /ota`, invalid images never switch slots) + USB (serialtap proxy flashing) |
| Console | UART0 (CH340 bridge) | **Native USB-Serial-JTAG** (logs straight out of the USB port) |
| Status LED | C3=GPIO8 / C6=GPIO15 | **None** (GPIO8 on this board is the UP button of the 5-key pad; `CONFIG_ESP_STATUS_LED=n` is mandatory — do not enable) |
| Provisioning AP | `ESP32_Blackbox` / `12345678` | `blackbox-c3` / `12345678` → 192.168.4.1 |

Probing capability (8 prober types), config hot-reload and the watchdog
(30s TWDT + panic reboot) match upstream.

## Build & Flash

ESP-IDF **v6.0** (same as the other projects in this repo). Board hardware
details: [../README.md](../README.md).

```powershell
# PowerShell (with the IDF environment activated)
cd blackbox
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor     # first flash writes the partition table too
```

Git Bash fallback (export scripts reject MSys; the top-level CMakeLists sets
`__CHECK_PYTHON 0` to skip the precheck — drive the cached toolchain directly):

```bash
cd blackbox
export IDF_PATH=~/esp/.espressif/v6.0/esp-idf
export PATH="/c/Espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:\
/c/Espressif/tools/ninja/1.12.1:/c/Espressif/tools/cmake/4.0.3/bin:$PATH"
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=$IDF_PATH/tools/cmake/toolchain-esp32c3.cmake
ninja -C build
```

After editing `sdkconfig.defaults`, always `rm -f sdkconfig` before
re-running cmake — the generated `sdkconfig` silently shadows the defaults
(a repo-wide trap).

Flash offsets: bootloader `0x0`, partition table `0x8000`, app `0x10000`.

## License

MIT — see [LICENSE](LICENSE) (same as the repo root LICENSE).
