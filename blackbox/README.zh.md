# blackbox — 网络探测终端（Prometheus blackbox_exporter 兼容）

[English](README.md) | [中文文档](README.zh.md)

合宙 ESP32-C3 板上的独立网络探测项目：WiFi 网络内的 HTTP/HTTPS/TCP/TLS/
DNS/ICMP/WebSocket 探测，配置经 Web 面板或 JSON 下发，指标以标准
Prometheus 格式输出——可被 Prometheus / MiBeeSteward / Grafana 直接抓取。

代码源自 [Mi-Bee-Studio/esp32-blackbox](https://github.com/Mi-Bee-Studio/esp32-blackbox)
（C3 SuperMini / C6 XIAO 双板工程），本目录为**本板移植版**，探测内核与上游逐字一致。

## 端点

| 端口 | 端点 | 说明 |
|------|------|------|
| 9090 | `/metrics` | 全部已配置目标的聚合 Prometheus 指标 |
| 9090 | `/probe?target=X&module=Y[&port=P]` | 按需单次探测（无需预配置目标） |
| 80 | `/` | Web 面板（探测配置 / WiFi 配网 / 重启） |
| 80 | `POST /ota` | 固件上传（**本板不可用**，见下） |

配置 JSON 的完整 schema（modules / targets / scrape_interval）见上游
[README](https://github.com/Mi-Bee-Studio/esp32-blackbox)。

## 与上游的差异（本板适配）

| 项 | 上游（C3 SuperMini / C6 XIAO） | 本板（Luatos ESP32-C3） |
|----|-------------------------------|------------------------|
| Flash | 4MB，OTA 双槽（1344K×2）+ SPIFFS | **2MB，OTA 双槽 960K×2 + 64K SPIFFS**（固件瘦身后装下：-Os + 断言静默 + WPA3-OWE 关（SAE 保留——路由是 WPA3-SAE-only）；app 975K，槽内 1% 余量） |
| 刷机 | Web OTA 或 USB | **Web OTA 可用**（:80 面板/`POST /ota`，校验失败不切槽）+ USB（serialtap 代理刷机） |
| 控制台 | UART0（CH340 桥） | **原生 USB-Serial-JTAG**（日志直出 USB 口） |
| 状态 LED | C3=GPIO8 / C6=GPIO15 | **无**（本板 GPIO8 是五键的 UP 键，`CONFIG_ESP_STATUS_LED=n` 强制关闭——勿开） |
| 配网热点 | `ESP32_Blackbox` / `12345678` | `blackbox-c3` / `12345678` → 192.168.4.1 |

探测能力（8 类 prober）、配置热加载、看门狗（30s TWDT + panic 重启）与上游一致。

## 构建与烧录

ESP-IDF **v6.0**（同本仓其它项目）。板级硬件信息见 [../README.zh.md](../README.zh.md)。

```powershell
# PowerShell（激活 IDF 环境后）
cd blackbox
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor     # 首次：分区表随固件一并写入
```

Git Bash 应急（MSys 拒绝 export 脚本；顶层 CMakeLists 已设 `__CHECK_PYTHON 0`
跳过预检，缓存工具链直跑）：

```bash
cd blackbox
export IDF_PATH=~/esp/.espressif/v6.0/esp-idf
export PATH="/c/Espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:\
/c/Espressif/tools/ninja/1.12.1:/c/Espressif/tools/cmake/4.0.3/bin:$PATH"
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=$IDF_PATH/tools/cmake/toolchain-esp32c3.cmake
ninja -C build
```

改过 `sdkconfig.defaults` 后必须 `rm -f sdkconfig` 再重新 cmake（根目录 sdkconfig
优先级更高，不删会被静默遮蔽——本仓通用坑）。

烧录偏移：bootloader `0x0`、分区表 `0x8000`、app `0x10000`。

## License

MIT —— 见 [LICENSE](LICENSE)（随本仓库根 LICENSE 一致）。
