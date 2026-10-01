# ESP32-C3 感知节点（WiFi CSI 感知终端）

[English](README.md) | [中文文档](README.zh.md)

![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)
![ESP-IDF](https://img.shields.io/badge/ESP-IDF-v6.0-green.svg)
![LVGL](https://img.shields.io/badge/LVGL-v9-yellow.svg)

**v2.0 —— 重新定位。** 这块板子现在是 homepulse（PC 侧感知引擎） 感知引擎的
专用 CSI 感知终端（经 serialtap USB 透明代理连接）。
旧的信息显示功能（时钟/天气/计时器/PC 监控及配套桌面工具）已全部移除。

**v2.1 —— 基线能力补齐：板端 Web OTA 刷机。** 2MB flash 经固件瘦身腾出
**OTA 双槽**（960K×2，app 偏移 0x10000 不变）：设置页新增固件上传
（校验失败不切槽，原固件无损）与重启；此前"2MB 放不下双槽只能 USB 刷机"
的旧结论作废。瘦身为代价的取舍（调试构建可逆，见 sdkconfig.defaults 注释）：
日志编译级 ERROR、WPA3-OWE 关闭（**SAE 保留**——2026-09-27 真机教训：家里路由
家里路由器是 WPA3-SAE-only，关 SAE 即失联）、LVGL 裁未用控件/flex/grid/
examples/8/14/16 号字体（bpm 标签 14→12 号）+ `-Oz` 极限优化。分区表变更：nvs 24K→16K（数据保留）、
新增 otadata；**升级首刷必须连分区表一起刷**。

> **用户手册：[docs/user-guide.zh.md](docs/user-guide.zh.md)** ——
> 页面说明、按键、校准、配网、组网、刷机与故障排查。

## 它做什么

```
板子 CSI ──#S1 行──> USB ──serialtap 透传──> homepulse DSP ──sense_status JSON──> LCD 镜像
```

- **CSI 采集**：WiFi 关联后，每包提取 16 个选定子载波 I/Q，以紧凑
  `#S1 seq t_ms rssi hex64` 行经 USB 串口输出（限频；可选网关 ping 激励稳定采样）。
- **任务看门狗（2026-09-24）**：LVGL 渲染任务每 5ms 一拍喂狗，渲染循环卡死
  （对象树竞争等）panic 重启自恢复——无人值守节点的兜底。本板不做 Web OTA：
  2MB flash 放不下双 1.3MB 槽，固件更新仍走 serialtap 代理刷机。
- **LCD 镜像（160×80）**：两个页面。
  - **Sense**（默认页）：PRESENT/ABSENT/CALIBRATING/PAUSED 大字、呼吸率+质量、
    运动条+分级。
  - **Node**：运行时长、内存、WiFi RSSI/SSID/IP、固件版本、CSI 计数诊断。
- **本地兜底**：无 PC 时板上跑粗略存在估计，UI 明示（`BR -- local mode`）。

## 按键

| 键 | 作用 |
|----|------|
| LEFT / RIGHT | 切换 Sense ↔ Node 页 |
| UP | **暂停/恢复 `#S1` 串流**（全局）。暂停时 Sense 页大字显示 `* PAUSED`，激励 ping 一并停止；恢复沿用最近一次 `sense_start` 参数 |
| DOWN | 把当前**感知诊断快照**写成一行串口日志（状态 + CSI 计数；serialtap 日志流可见，用于现场排障留痕） |
| CENTER（Sense 页） | **重新校准**：本地基线复位，并让 PC 引擎重学（30 秒，房间需无人） |
| CENTER（Node 页） | 诊断快照（同 DOWN） |

## 板端 Web（`http://<板子IP>/`）

WiFi 关联后板子自带设置页（`app_web.c`，esp_http_server :80，无鉴权——家庭
LAN 内使用）。IP 见 LCD **Node 页**（homepulse 面板的 WiFi 配网区也能查）。
手机连同一 WiFi 即可**在房间外**操作——解开"校准要求无人、按钮要求有人在场"
的死结：

- **状态**：有人/无人、动作、呼吸、采样率、RSSI、WiFi/IP，2 秒自刷
- **校准**：立即 / **离开后校准（60s 布防，可取消）**（与 homepulse 面板同语义）
- **WiFi 配网**：扫描列表 + 密码连接（换网络不用接 USB）

REST：`GET /api/status` · `POST /api/calibrate {"delay_s":N|0,"cancel":true}` ·
`POST /api/wifi/scan` · `POST /api/wifi/connect {"ssid","pass"}`。

## 固件结构

| 文件 | 职责 |
|------|------|
| `main/app_sense.c` | CSI 回调 → 队列 → `#S1` 串流任务 + 本地粗估 + 协议命令 |
| `main/ui/ui_common.h` | 布局体系：`UI_LH_*` 真实行高 + `UI_ROW_ASSERT` 编译期溢出断言 |
| `main/ui/ui_sense.c` / `ui_node.c` | 两个页面 |
| `main/app_wifi.c` | STA + NVS 凭据 + `wifi_scan`/`wifi_connect` 配网 |
| `main/app_protocol.c` | USB 串口 JSON 行协议（cJSON） |

## 协议（USB 串口，JSON 行）

`hello` · `ui_page{page:sense|node}` · `wifi_scan` · `wifi_connect{ssid,pass}` ·
`sense_start{max_hz,stimulus_hz}` · `sense_stop` · `sense_calibrate{delay_s?}` ·
`sense_status{...}`（PC 推送，不回 ack）· `sense_info`；
遥测 `#S1 seq t_ms rssi hex64`（会话头 `#S1-HELLO`）。

`#S1` 格式与 `homepulse/internal/sense/parser.go` 是契约 —— 禁止单方修改。

## 防溢出布局

v1 的 UI 溢出 80px 屏幕，原因是行位置按"字号≈占位高度"估放，而 LVGL 实际
行高更大（montserrat_12 → 15px）。v2 规则（在 `ui_common.h` 强制）：

1. 行 y/行高必须用 `UI_LH_8/12/14/16/22` 常量（取自 LVGL 字体源码的真实
   `.line_height`）；
2. 每行声明 `UI_ROW_ASSERT(name, y, lh)` —— 溢出现在是**编译错误**；
3. `ui_row_label()` 运行期二次检查，越界拒绝创建并打 E 日志；
4. 页面统一建在 `ui_content_root()` 上（15px 标题栏下 160×65）。

## 编译与刷机

```bash
idf.py build

# 经 serialtap 代理刷机（自动让口 → esptool → 回采；设备名按你的 serialtap 命名）
serialtap flash esp32s3-jtag build/bootloader/bootloader.bin@0x0 \
  build/partition_table/partition-table.bin@0x8000 \
  build/wifi_csi_sensing.bin@0x10000
```

配网：经 serialtap 代理端点（或任何串口终端）发送
`{"cmd":"wifi_connect","ssid":"...","pass":"..."}\n`。

构建说明：`components/esp_lcd_st7735/` 是 **vendor 进来的**
`waveshare/esp_lcd_st7735` 1.0.1，带一行 IDF v6.0 API 适配
（`rgb_endian` → `data_endian`，上游字段改名）。不要删除它重新拉取 ——
组件管理器拉的版本在 IDF v6.0 下编不过。

## 硬件

| 部件 | 规格 |
|------|------|
| 主控 | ESP32-C3 (RISC-V) |
| 屏幕 | ST7735S 160×80 LCD（合宙 Air101 LCD） |
| 按键 | 5 个（LEFT=5 UP=8 CENTER=4 DOWN=13 RIGHT=9） |
| 接口 | USB-Serial-JTAG |

LCD 引脚：SCLK=2 MOSI=3 DC=6 CS=7 RST=10（panel drive gap 1/26 —— 这块屏
调稳的关键参数，重新初始化时保留）。

## 许可

MIT
