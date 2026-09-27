# AGENTS.md — blackbox（工程级约定）

本目录是 [esp32-blackbox](https://github.com/Mi-Bee-Studio/esp32-blackbox)
在本板（合宙 ESP32-C3）的移植子项目。改代码前先读本文件；板级硬件事实见
根 [README](../README.zh.md)，用户视角与上游差异见 [README](README.zh.md)。

## 与上游的同步纪律

- `main/probe_*.c`、`config_manager.c`、`metrics_server.c`、`web_server.c`、
  `wifi_manager.c` 与上游**逐字一致**——上游修 bug 后整文件覆盖拷回，
  不要在拷贝件里做本板定制（板级差异全部收敛在下列三个文件）：
  - `CMakeLists.txt`（项目名 blackbox + `__CHECK_PYTHON 0`）
  - `sdkconfig.defaults`（2MB flash / USB-JTAG 控制台 / 无 LED / 热点名）
  - `partitions.csv`（2MB 单 factory 槽 + SPIFFS）
  - `main/main.c` 仅一处板名日志字符串差异
- 已知的上游移植差异清单（README 表格）变更时同步更新双语文档。

## 硬约束（红线）

| 项 | 值 | 原因 |
|----|-----|------|
| `CONFIG_ESP_STATUS_LED=n` | 必须保持 | GPIO8 是五键 UP 键——开 LED 会驱动按键脚 |
| OTA 双槽 960K×2 | web 刷机基线能力 | app 946K/槽 983K 仅 **4% 余量**——加依赖前先看尺寸 |
| 瘦身项 | -Os + 断言静默 + WPA3-OWE 关（SAE 必须保留：路由 WPA3-SAE-only） | 都是给 OTA 槽腾的 flash；当前 ~975K 仅 1% 余量，加代码前先量尺寸 |
| `CONFIG_ESP_TASK_WDT_TIMEOUT_S=30` | 保持 | 探测为同步网络调用，模块超时可达 120s |
| SPIFFS `storage` @0x1F0000 0x10000 | 位置勿动 | 探测配置 JSON 在其中，移位即丢配置 |

## 构建与烧录

ESP-IDF v6.0，PowerShell `idf.py`；Git Bash 直跑 ninja（顶层已设
`__CHECK_PYTHON 0`），配方见 README。烧录偏移 0x0 / 0x8000 / 0x10000；
app 偏移变化即需连分区表一起刷。改 `sdkconfig.defaults` 后
`rm -f sdkconfig` 再 cmake（遮蔽坑）。

## 排障备忘

- 探测全 FAIL：先看 WiFi 是否关联（:80 面板状态卡）；ICMP 需网关放行
  SOCK_RAW（lwip 默认支持，路由器侧可能拦）。
- 配置损坏回不到有效态：SPIFFS `/spiffs/blackbox.json` 删掉重启即回
  工厂默认（模块 4 个、目标以上游默认）。
