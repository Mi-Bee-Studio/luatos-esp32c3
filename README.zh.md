# Luatos ESP32-C3（感知节点主板）

[English](README.md) | [中文文档](README.zh.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/luatos-esp32c3/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/luatos-esp32c3/actions/workflows/build.yml)

主板目录规范的一块板：**合宙 ESP32-C3 CORE 核心板**；合宙 Air101 LCD（ST7735S 0.96″）
是**外接显示扩展板**，不是核心板的一部分——接它的项目放 `air101-lcd/` 子目录，
裸核心板项目放顶层。**本仓库按"主板为根"规范组织**：

```
luatos-esp32c3/
├── README.md          # 本文件：这块板的一切硬件信息
├── <project>/         # 顶层项目：只用核心板即可运行（不接 Air101 LCD）
│   ├── CMakeLists.txt / main/ / sdkconfig.defaults / main/idf_component.yml
│   └── README.md      # 项目说明 + 编译/烧录命令
└── air101-lcd/        # Air101 LCD 扩展板项目目录
    └── <project>/     # 依赖 Air101 LCD 的项目放这里（三件套同上）
```

规范要点：

- **主板目录名** = 板子名（kebab-case），根 README 只写硬件、不写业务；
- **项目目录独立可编译**：自带完整 ESP-IDF 工程三件套（顶层 CMakeLists、
  `main/`、`sdkconfig.defaults`），`cd <project> && idf.py build` 即出固件；
- **扩展板为子目录**：接 Air101 LCD 的项目放 `air101-lcd/<project>/`；
  以后不接扩展板的裸核心板项目一律放顶层；
- 项目间不共享代码；需要共性时先拷贝，稳定后再考虑抽组件。

### 固件基线规范（全家桶强制）

两条基线对所有 MiBee 固件仓强制执行，主板仓内**每个项目**都必须满足：

1. **看门狗：必须启用**。不允许裸奔主循环——任务要么订阅 TWDT 按周期喂狗，
   要么（RP2040）启用硬件看门狗；
2. **Web/API 固件升级（OTA）：硬件允许则必须提供**。本板有 WiFi、且 flash 里
   已规划 OTA 双槽（960K×2），故每个项目都要带板端 web 刷机能力；有线烧录
   （serialtap/esptool）是兜底恢复手段，不能替代 OTA。

| 项目 | 看门狗 | Web/API OTA |
|------|--------|-------------|
| wifi-csi-sensing | ✅ 任务订阅 TWDT（`esp_task_wdt_add`/`reset`） | ✅ OTA 双槽 + 板端 web 刷机 |
| blackbox | ✅ 任务订阅 TWDT | ✅ OTA 双槽 + 板端 web 刷机 |

---

## 板子概要

| 项目 | 值 |
|------|-----|
| 模组/芯片 | ESP32-C3 —— RISC-V 单核，rev v0.4 实测 |
| Flash | 2MB，**板载外置、DIO 两线接法**（只占 IO14–17，未引出；IO12/13 因此腾出） |
| 无线 | 2.4GHz WiFi b/g/n + Bluetooth 5 (LE)；**CSI 已启用**（`CONFIG_ESP_WIFI_CSI_ENABLED`） |
| USB | **原生 USB-Serial-JTAG，无 USB-UART 桥芯片**（控制台/烧录同一口；USB D-/D+ = IO18/19） |
| 显示 | **外接扩展板** 合宙 Air101 LCD：ST7735S 0.96″ 160×80，SPI 四线（SCLK/MOSI/DC/CS + RST）——相关项目在 `air101-lcd/` |
| 按键 | 板载 RST、BOOT（=IO9，按住上电进下载）；五向键 LEFT/UP/CENTER/DOWN/RIGHT（低电平有效） |
| 板载 LED | D4 = IO12、D5 = IO13（固件未用；IO13 同时接五向键 DOWN） |
| 尺寸/引出 | 21 × 51 mm，板边邮票孔 2×16 = 32 引脚 |
| 分区 | 自定义 OTA 双槽 960K×2（两个子项目均支持板端 web 刷机） |

## 引脚位置图（USB-C 朝上，正面/元件面视角；括号内为合宙官方天线朝上编号）

> 全家桶标准视角为 **USB-C 朝上**（标准样板见 `esp32-s3-zero`）。合宙官方管脚图为天线朝上，
> 本图按 180° 旋转重绘，官方引脚号逐脚标注在括号内，可与官方图直接对照。

```
                 ┌─ USB-C ─┐
  GND(32) ◎     │          │     ◎ 5V(16)
   5V(31) ◎     │          │     ◎ PWB(15)
  IO9(30) ◎     │  CORE-   │     ◎ GND(14)   ← IO9 = 五向键 RIGHT = BOOT(IO9)
  IO8(29) ◎     │  ESP32   │     ◎ 3V3(13)   ← IO8 = 五向键 UP
  IO4(28) ◎     │    C3    │     ◎ RST(12)   ← IO4 = 五向键 CENTER
  IO5(27) ◎     │  21×51mm │     ◎ NC(11)    ← IO5 = 五向键 LEFT
  3V3(26) ◎     │          │     ◎ IO13(10)  ← IO13 = 五向键 DOWN + LED D5
  GND(25) ◎     │          │     ◎ U0TX(09)  ← U0TX = IO21
 IO11(24) ◎     │          │     ◎ U0RX(08)  ← U0RX = IO20；IO11 = VDD_SPI（烧 eFuse 才可作 GPIO）
   IO7(23) ◎    │  [BOOT]  │     ◎ GND(07)   ← IO7 = LCD CS
   IO6(22) ◎    │   [RST]  │     ◎ IO19(06)  ← IO6 = LCD DC；IO19 = USB D-
  IO10(21) ◎    │          │     ◎ IO18(05)  ← IO10 = LCD RST；IO18 = USB D+
   IO3(20) ◎    │          │     ◎ IO12(04)  ← IO3 = LCD MOSI；IO12 = LED D4
   IO2(19) ◎    │          │     ◎ IO1(03)   ← IO2 = LCD SCLK
  3V3(18) ◎     │          │     ◎ IO0(02)   ← 空闲（ADC0/ADC1，兼 UART1 复用）
  GND(17) ◎     │          │     ◎ GND(01)
                └──────────┘
                ~ 2.4G 天线 ~
      左排（官方 32→17）   右排（官方 16→01）
```

要点：

- **左排**自 USB 端向下（官方 32→17）：`GND, 5V, IO9, IO8, IO4, IO5, 3V3, GND, IO11, IO7, IO6, IO10, IO3, IO2, 3V3, GND`；
- **右排**自 USB 端向下（官方 16→01）：`5V, PWB, GND, 3V3, RST, NC, IO13, U0TX, U0RX, GND, IO19, IO18, IO12, IO1, IO0, GND`；
- **LCD 与五向键几乎全在左排**（唯一例外是右排的 DOWN 键）：LCD 信号属外接
  Air101 扩展板——SCLK=IO2 · MOSI=IO3 · RST=IO10 · DC=IO6 · CS=IO7；键 CENTER=IO4 · LEFT=IO5 · UP=IO8 · RIGHT=IO9 · DOWN=IO13；
- **U0RX/U0TX = IO20/IO21**（UART0 引出；控制台走 USB-Serial-JTAG，不占它）；**IO18/19 = USB D-/D+**，勿挪用；
- **IO14–17 未引出**：板载外置 flash 占用（DIO 接法只用这 4 根，IO12/13 因此空闲）；
- **IO12 = 板载 LED D4、IO13 = 板载 LED D5**（IO13 同时是五向键 DOWN，按下接地；这两个脚挪用前先看板载 LED 接法）；
- **IO11** 默认是 VDD_SPI（flash 供电脚），要作 GPIO 需一次性烧录 `VDD_SPI_AS_GPIO` eFuse（本板 flash 供电固定 3.3V，可烧）；
- **PWB**：3.3V 输出使能（拉高导通），要给外设供电时拉高；IO0/IO1 空闲可用（ADC0/ADC1，兼 UART1 复用脚）。

## 实际引脚占用（以固件源码为准：`air101-lcd/wifi-csi-sensing/main/app_lcd.c`、`app_button.c`）

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

详见 [`air101-lcd/wifi-csi-sensing/README.zh.md`](air101-lcd/wifi-csi-sensing/README.zh.md) 与
[用户手册](air101-lcd/wifi-csi-sensing/docs/user-guide.zh.md)。

## 注意事项

- **无 USB-UART 桥**：串口就是 C3 的 USB-Serial-JTAG，`CONFIG_USB_SERIAL_JTAG_ENABLED=y`、
  控制台走同一口。#S1 遥测、sense_start 等 JSON 命令、日志共用这一条 USB 串口。
- **五向键压在 strapping 脚上**：RIGHT = IO9 = 板载 BOOT——上电/复位瞬间按着
  RIGHT 会直接进下载模式；UP = IO8——按住等效外部下拉，**上电/进下载/刷机时
  别按着 UP**（会导致下载失败）。
- C3 单核跑不动完整 DSP：板端**只采不发**（CSI → #S1 行），频谱分析/存在判定在 PC 侧
  homepulse 完成，状态 JSON 回推 LCD 镜像。
- LVGL 陷阱（`air101-lcd/wifi-csi-sensing/main/ui/`）：`set_text_fmt` 带 `%f` 必崩
  （用 `ui_label_fmt`）；跨任务切页走请求标志，不得直接调 lv_scr_load。
- **LCD 组件已 vendor**：`air101-lcd/wifi-csi-sensing/components/esp_lcd_st7735/` 是
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
直接 ninja（构建配方与工程约定见 [air101-lcd/wifi-csi-sensing/AGENTS.md](air101-lcd/wifi-csi-sensing/AGENTS.md)）。

## 项目索引

**顶层项目（裸核心板，不接 Air101 LCD）：**

| 项目 | 说明 |
|------|------|
| [blackbox](blackbox/README.zh.md) | 网络探测终端（Prometheus blackbox_exporter 兼容）：8 类探测 + Web 面板 + :9090 指标，esp32-blackbox 的本板移植版 |

**Air101 LCD 扩展板项目（`air101-lcd/`）：**

| 项目 | 说明 |
|------|------|
| [wifi-csi-sensing](air101-lcd/wifi-csi-sensing/README.zh.md) | WiFi CSI 感知终端：CSI 采集 + #S1 流 + Sense/Node 双页 LVGL UI，homepulse 感知引擎的板端节点（v2.0） |

## License

MIT —— 见 [LICENSE](LICENSE)（子目录项目各自携带）。
