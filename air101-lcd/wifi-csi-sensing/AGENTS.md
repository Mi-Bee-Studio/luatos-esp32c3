# AGENTS.md — wifi-csi-sensing（工程级约定）

项目级工程笔记：改代码前先读。板级硬件事实（芯片/引脚/按键/供电）见根
[README](../../README.zh.md)，用户视角功能见
[用户手册](docs/user-guide.zh.md)。

## 硬约束（读后再动代码）

| 项 | 值 | 红线 |
|----|-----|------|
| 芯片 | ESP32-C3（RISC-V 单核） | 单核跑不动完整 DSP——板端只采不发（CSI → 协议行），重分析在 PC 侧 |
| ESP-IDF | v6.0 | 组件/API 以 v6.0 为准，勿照抄 v5.x 例程 |
| 控制台 | USB-Serial-JTAG（`CONFIG_USB_SERIAL_JTAG_ENABLED`） | 唯一 USB I/O：遥测、JSON 命令、日志共用 |
| 分区 | 自定义双槽 OTA（`partitions.csv` 960K×2） | web 刷机基线能力；app 923K/槽 983K **仅 ~6% 余量**——加代码先看尺寸 |
| CSI | `CONFIG_ESP_WIFI_CSI_ENABLED=y` | 16 子载波 I/Q，协议行限速输出 |
| 瘦身红线 | 见 sdkconfig.defaults 注释 | 日志=ERROR、WPA3-OWE 关（**SAE 必须保留**：路由是 WPA3-SAE-only）、LVGL 裁控件/flex/grid/8/14/16 号字体、-Oz——都是为 OTA 双槽腾的 flash；恢复任一项前先确认 app 仍 <960K（当前 ~976K 仅 1% 余量）。调试期可本地临时换单槽分区+INFO 日志构建诊断固件。**路由限流教训**：刷了关 SAE 的固件后，路由 SAE 防爆破会限流该 MAC ~10-20 分钟，期间即使刷回 SAE 固件也关联失败——等窗口过期再评估 |

## 协议与链路

- 出线双宿：`app_usb_send_str` 内同时发 USB 串口与 WFP TCP（`app_net.c`，
  :7788）；入线（USB 与 TCP）统一走 `app_usb_dispatch_line` → 同一份 JSON 分发。
- 改协议行格式时同步更新两份 README 与用户手册的协议章节。

## LVGL / UI 已知陷阱

- `set_text_fmt` 带 `%f` 必崩（浮点格式化崩溃）——用 `ui_label_fmt`；
- 跨任务切页必须走请求标志（主循环内切换），不得直接调 `lv_scr_load`；
- 行高必须 ≥ 字号（UI_ROW_ASSERT 布局体系会断言）；
- 看门狗挂在 lvgl 渲染循环（5ms 喂狗，5s 超时 panic 重启，
  `CONFIG_ESP_TASK_WDT_PANIC=y`）——对象树竞争/渲染挂死的自愈兜底。

## vendor 组件

`components/esp_lcd_st7735/` = waveshare 1.0.1 + 一行 IDF v6.0 API 适配
（`rgb_endian` → `data_endian`）。组件管理器拉的版本在 v6.0 下编不过——
**勿删 vendor 目录、勿在 `main/idf_component.yml` 恢复该依赖**。

## 按键语义（五键）

实现在 `main/ui/ui_main.c`（page_key_handler）+ `main/app_sense.c`：
LEFT/RIGHT 切页 · UP 暂停/恢复串流 · DOWN 诊断快照进串口日志 ·
CENTER 按页分派（Sense 页=重校准，Node 页=诊断快照）。
改按键行为时同步更新根 README 按键表与用户手册 §4。

## 构建与烧录

```powershell
# PowerShell 激活 IDF 环境（Git Bash 会被 export 脚本的 MSys 检测拒绝）
cd wifi-csi-sensing
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

Git Bash 应急：顶层 `CMakeLists.txt` 已设 `__CHECK_PYTHON 0` 跳过 MSys 下的
Python 预检，配置一次后可直接用缓存工具链跑 ninja（需要本机装有 IDF v6.0
与工具链，路径按实际安装调整）。

烧录口被串口代理类工具占用时会拒绝 idf.py——先停代理或用其刷机通道。

## CI

`.github/workflows/build.yml`：espressif/idf:v6.0 容器 esp32c3 构建，
tag 推送自动发 release——**改固件必须保证 `idf.py build` 通过**。
提交信息用 conventional commits（`feat: / fix: / docs: / chore:`）。
