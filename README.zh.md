# Luatos ESP32-C3 + Air101 LCD（感知节点主板）

[English](README.md) | [中文文档](README.zh.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/luatos-esp32c3-air101lcd/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/luatos-esp32c3-air101lcd/actions/workflows/build.yml)

主板目录规范的一块板。**本仓库按"主板为根"规范组织**：

```
luatos-esp32c3-air101lcd/
├── README.md          # 本文件：这块板的一切硬件信息
└── <project>/         # 每个用这块板做的项目一个目录（按能力命名）
    ├── CMakeLists.txt / main/ / sdkconfig.defaults / main/idf_component.yml
    └── README.md      # 项目说明 + 编译/烧录命令
```

规范要点：

- **主板目录名** = 板子名（kebab-case），根 README 只写硬件、不写业务；
- **项目目录独立可编译**：自带完整 ESP-IDF 工程三件套（顶层 CMakeLists、
  `main/`、`sdkconfig.defaults`），`cd <project> && idf.py build` 即出固件；
- 项目间不共享代码；需要共性时先拷贝，稳定后再考虑抽组件。

---

## 板子概要

| 项目 | 值 |
|------|-----|
| 模组/芯片 | ESP32-C3 —— RISC-V 单核，rev v0.4 实测 |
| Flash | 2MB，**板载外置、DIO 两线接法**（只占 IO14–17，未引出；IO12/13 因此腾出） |
| 无线 | 2.4GHz WiFi b/g/n + Bluetooth 5 (LE)；**CSI 已启用**（`CONFIG_ESP_WIFI_CSI_ENABLED`） |
| USB | **原生 USB-Serial-JTAG，无 USB-UART 桥芯片**（控制台/烧录同一口；USB D-/D+ = IO18/19） |
| 显示 | 合宙 Air101 LCD：ST7735S 0.96″ 160×80，SPI 四线（SCLK/MOSI/DC/CS + RST） |
| 按键 | 板载 RST、BOOT（=IO9，按住上电进下载）；五向键 LEFT/UP/CENTER/DOWN/RIGHT（低电平有效） |
| 板载 LED | D4 = IO12、D5 = IO13（固件未用；IO13 同时接五向键 DOWN） |
| 尺寸/引出 | 21 × 51 mm，板边邮票孔 2×16 = 32 引脚 |
| 分区 | 自定义 OTA 双槽 960K×2（两个子项目均支持板端 web 刷机） |

## 引脚位置图（天线朝上、USB-C 朝下，元件面视角；引脚号同合宙官方管脚图）

```
                  ~ 2.4G antenna ~
       GND ◎│                 │◎ GND
       IO0 ◎│   CORE-ESP32    │◎ 3V3
       IO1 ◎│    ESP32-C3     │◎ IO2    ← LCD SCLK
      IO12 ◎│    21 x 51 mm   │◎ IO3    ← LCD MOSI
      IO18 ◎│                 │◎ IO10   ← LCD RST
      IO19 ◎│                 │◎ IO6    ← LCD DC
       GND ◎│ [RST]   [BOOT]  │◎ IO7    ← LCD CS
      U0RX ◎│                 │◎ IO11   (VDD_SPI - 烧 eFuse
      U0TX ◎│                 │◎ GND     才可作 GPIO)
      IO13 ◎│                 │◎ 3V3
        NC ◎│                 │◎ IO5    ← 五向键 LEFT
       RST ◎│                 │◎ IO4    ← 五向键 CENTER
       3V3 ◎│     ┌─────┐     │◎ IO8    ← 五向键 UP
       GND ◎│     │USB-C│     │◎ IO9    ← 五向键 RIGHT = BOOT(IO9)
       PWB ◎│     └─────┘     │◎ 5V
        5V ◎│                 │◎ GND
            └─────────────────┘
         左排（01–16）     右排（17–32）
```

要点：

- **左排**自天线端向下（01–16）：`GND, IO0, IO1, IO12, IO18, IO19, GND, U0RX, U0TX, IO13, NC, RST, 3V3, GND, PWB, 5V`；
- **右排**自天线端向下（17–32）：`GND, 3V3, IO2, IO3, IO10, IO6, IO7, IO11, GND, 3V3, IO5, IO4, IO8, IO9, 5V, GND`；
- **LCD 与五向键几乎全在右排**（唯一例外是左排的 DOWN 键）：SCLK=IO2 · MOSI=IO3 · RST=IO10 · DC=IO6 · CS=IO7；键 CENTER=IO4 · LEFT=IO5 · UP=IO8 · RIGHT=IO9 · DOWN=IO13；
- **U0RX/U0TX = IO20/IO21**（UART0 引出；控制台走 USB-Serial-JTAG，不占它）；**IO18/19 = USB D-/D+**，勿挪用；
- **IO14–17 未引出**：板载外置 flash 占用（DIO 接法只用这 4 根，IO12/13 因此空闲）；
- **IO12 = 板载 LED D4、IO13 = 板载 LED D5**（IO13 同时是五向键 DOWN，按下接地；这两个脚挪用前先看板载 LED 接法）；
- **IO11** 默认是 VDD_SPI（flash 供电脚），要作 GPIO 需一次性烧录 `VDD_SPI_AS_GPIO` eFuse（本板 flash 供电固定 3.3V，可烧）；
- **PWB**：3.3V 输出使能（拉高导通），要给外设供电时拉高；IO0/IO1 空闲可用（ADC0/ADC1，兼 UART1 复用脚）。

## 实际引脚占用（以固件源码为准：`wifi-csi-sensing/main/app_lcd.c`、`app_button.c`）

### LCD（ST7735S，SPI）

| 信号 | GPIO |
|------|------|
| SCLK | 2 |
| MOSI | 3 |
| DC   | 6 |
| CS   | 7 |
| RST  | 10 |

驱动组件：`waveshare/esp_lcd_st7735 ^1.0.1` + LVGL `^9.5.0`。

### 五向键（五键全部启用，GPIO 低电平有效）

| 键 | GPIO | 作用 |
|----|------|------|
| LEFT   | 5  | 切上一页 |
| RIGHT  | 9  | 切下一页 |
| UP     | 8  | 暂停/恢复 CSI 串流（全局；Sense 页显示 `* PAUSED`，恢复沿用原采样参数） |
| DOWN   | 13 | 诊断快照写一行串口日志（状态 + CSI 计数，serialtap 日志流可见） |
| CENTER | 4  | Sense 页 = 重新校准；Node 页 = 诊断快照 |

详见 [`wifi-csi-sensing/README.zh.md`](wifi-csi-sensing/README.zh.md) 与
[用户手册](wifi-csi-sensing/docs/user-guide.zh.md)。

## 注意事项

- **无 USB-UART 桥**：串口就是 C3 的 USB-Serial-JTAG，`CONFIG_USB_SERIAL_JTAG_ENABLED=y`、
  控制台走同一口。#S1 遥测、sense_start 等 JSON 命令、日志共用这一条 USB 串口。
- **五向键压在 strapping 脚上**：RIGHT = IO9 = 板载 BOOT——上电/复位瞬间按着
  RIGHT 会直接进下载模式；UP = IO8——按住等效外部下拉，**上电/进下载/刷机时
  别按着 UP**（会导致下载失败）。
- C3 单核跑不动完整 DSP：板端**只采不发**（CSI → #S1 行），频谱分析/存在判定在 PC 侧
  homepulse 完成，状态 JSON 回推 LCD 镜像。
- LVGL 陷阱（`wifi-csi-sensing/main/ui/`）：`set_text_fmt` 带 `%f` 必崩
  （用 `ui_label_fmt`）；跨任务切页走请求标志，不得直接调 lv_scr_load。
- **LCD 组件已 vendor**：`wifi-csi-sensing/components/esp_lcd_st7735/` 是
  waveshare 1.0.1 + IDF v6.0 API 适配（`rgb_endian` → `data_endian`）。
  组件管理器拉的版本在 v6.0 下编不过，勿删勿重拉。

## 与 serialtap（中间层）的适配点

- 插 USB 按 USB-Serial-JTAG 枚举，设备名形态与 S3 相同（实测 `esp32c3` /
  `esp32s3-jtag` 一族）；serialtap 的发现/命名/采集/透传/刷机链路直接可用；
- 固件 ~1.4MB，刷写耗时同量级，代理刷机（让口 → esptool → 回采）已验证可用；
- 与 serialtap 共享端点时，代理占用会让 idf.py 烧录被拒——先停代理或走
  `serialtap flash`。

## 工具链

**ESP-IDF v6.0**（本板实配；`sdkconfig.defaults` 即按 v6.0 + C3 调好）。
Git Bash 里 idf.py 会被 MSys 检测拒绝——构建走 PowerShell，或用缓存工具链
直接 ninja（构建配方与工程约定见 [wifi-csi-sensing/AGENTS.md](wifi-csi-sensing/AGENTS.md)）。

## 项目索引

| 项目 | 说明 |
|------|------|
| [wifi-csi-sensing](wifi-csi-sensing/README.zh.md) | WiFi CSI 感知终端：CSI 采集 + #S1 流 + Sense/Node 双页 LVGL UI，homepulse 感知引擎的板端节点（v2.0） |
| [blackbox](blackbox/README.zh.md) | 网络探测终端（Prometheus blackbox_exporter 兼容）：8 类探测 + Web 面板 + :9090 指标，esp32-blackbox 的本板移植版 |

## License

MIT —— 见 [LICENSE](LICENSE)（子目录项目各自携带）。
