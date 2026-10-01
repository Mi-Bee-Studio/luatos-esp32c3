# blink — 裸核心板基线工程（测试固件）

> **顶层项目**：只依赖合宙 ESP32-C3 CORE 核心板，**不接 Air101 LCD**。
> 同构拷贝自 [`esp32-s3-zero/blink`](../../esp32-s3-zero/blink)（共性先拷贝规范）。

## 功能

- 板载 LED **D4（GPIO12）** 每秒翻转；
- **BOOT 键（GPIO9）** 按住常亮（交互自检），松开恢复闪烁；
- 每 10 秒一条心跳日志（`uptime` / `heap`），供 serialtap 持续采集验证；
- 板端 **Web 维护页 :80**：WiFi 配网 / 固件 OTA / 重启（未配网时兜底热点
  `blink-c3` / `12345678` → `192.168.4.1`）；
- **看门狗**：ESP-IDF TWDT 5s 超时 panic（主循环 1s 一拍喂狗）——基线规范强制；
- **OTA**：自定义分区表 **OTA 双槽 960K×2**（2MB flash 硬约束，app 起始
  `0x10000`），Web 页流式写备用槽 → 校验 → 切槽 → 重启。

## 构建与烧录

ESP-IDF **v6.0**（本机 eim 安装：`C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1`；
CI 用 `espressif/idf:v6.0` 容器）。Git Bash 下 idf.py 会被 MSys 检测拒绝——
构建走 PowerShell（先 `Remove-Item Env:MSYSTEM` 若从 Git Bash 启动）：

```powershell
cd blink
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

- 控制台走原生 **USB-Serial-JTAG**（本板无 USB-UART 桥）；
- 也可用 release 里的 `flash_blink.sh/.bat`（bootloader + 分区表 + app 三件齐刷）。

## 固件基线规范核对

| 基线 | 状态 |
|------|------|
| 看门狗 | ✅ TWDT 5s panic，主循环喂狗 |
| Web/API OTA | ✅ OTA 双槽 + `POST /ota` 流式写槽 |

## 引脚占用

| 信号 | GPIO | 说明 |
|------|------|------|
| LED D4 | 12 | 板载 LED（每秒翻转） |
| BOOT | 9 | 板载按键，按住上电进下载模式；固件内作交互输入 |
| USB D-/D+ | 18/19 | 原生 USB，勿挪用 |

其余硬件事实见仓库根 [README.zh.md](../README.zh.md)。
