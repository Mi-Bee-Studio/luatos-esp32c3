#ifndef APP_SENSE_H
#define APP_SENSE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 感知显示状态（PC 推送优先，本地估计兜底）
typedef struct {
    bool has_data;       // 收到过任何 CSI/PC 数据
    bool pc_online;      // PC 推送的 sense_status 新鲜（<10 s）
    bool present;        // 存在检测
    float motion;        // 0..1
    char motion_cls[8];  // idle | light | walk（值拷贝；曾用指针指向栈上缓冲，悬垂）
    float bpm;           // 呼吸率（0 = 无可信估计）
    float quality;       // 0..1
    float rate_hz;       // 实测 CSI 样本率
    int8_t rssi;         // 最近 RSSI
    bool calibrating;    // 本地基线采集 / PC 校准中（无 PC 数据时）
    bool streaming;      // 正在向 PC 串流
    uint32_t seq;        // 已发样本序号（调试）
} sense_status_t;

// 初始化：注册协议命令（sense_start/stop/calibrate/status/info）。
// 须在 app_protocol_init 之后调用。
void app_sense_init(void);

// WiFi 关联状态钩子（app_wifi 事件处理器调用；关联后开 CSI 采集）。
void app_sense_on_wifi(bool connected);

// Sense 页 CENTER 键：请求重新校准（本地 + 通知 PC 侧重开流）。
void app_sense_request_recalibrate(void);

// 延迟校准（布防-离开）：校准要求房间无人，但按键/Web 都要人在场——
// 矛盾。布防 N 秒后自动触发（重布防 = 重新计时），供人离开房间。
void app_sense_arm_calibrate(int delay_s);
void app_sense_cancel_calibrate(void);
int64_t app_sense_calib_pending_ms(void); // 0 = 无布防

// UP 键：暂停/恢复 #S1 串流（走 sense_stop/sense_start 同款内部路径；
// 暂停时停掉激励 ping，恢复沿用最近一次 sense_start 的 max_hz/stimulus_hz）。
void app_sense_toggle_streaming(void);

// DOWN 键 / Node 页 CENTER 键：把当前感知诊断快照写成一行串口日志
// （serialtap 日志流可见，用于现场排障留痕）。
void app_sense_dump_diag(void);

// UI 读取当前状态（线程安全快照）。
bool app_sense_get_status(sense_status_t *out);

// CSI 累计诊断（Node 健康页展示）。
void app_sense_get_diag(uint32_t *csi_total, uint32_t *qdrop);

#ifdef __cplusplus
}
#endif

#endif // APP_SENSE_H
