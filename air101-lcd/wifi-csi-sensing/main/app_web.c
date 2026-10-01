// app_web.c: 板端 Web（esp_http_server :80）。端点：
//   GET  /               内置设置页（状态 / 校准含布防 / WiFi 配网 / OTA 刷机 / 重启）
//   GET  /api/status     感知 + 链路 + WiFi 状态 JSON
//   POST /api/calibrate  {"delay_s":0|N} / {"cancel":true}
//   POST /api/wifi/scan  扫描并返回 AP 列表（阻塞 ~2s，期间 CSI 停更）
//   POST /api/wifi/connect {"ssid":..,"pass":..}
//   POST /ota            固件上传：流式写备用 OTA 槽 → esp_ota_end 校验 → 切槽
//                        → 延迟重启；校验失败不切槽，原固件无损（基线能力）
//   POST /api/reboot     延迟重启（应答发完再重启）
// 页面与 wifipulse 面板同一套交互语义；无鉴权（家庭 LAN 内使用，与
// 姐妹相机仓 :80 的定位一致）。
#include "app_web.h"

#include <string.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "app_sense.h"
#include "app_wifi.h"
#include "app_protocol.h"
#include "cJSON.h"
#include "ui/ui_common.h" // APP_FW_VERSION

static const char *TAG = "APP_WEB";
static httpd_handle_t s_server;

/* ---------- 内置页面（单文件，无外部资源） ---------- */
static const char PAGE_HTML[] =
"<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>感知节点</title><style>"
":root{--bg:#0f1525;--card:#171f33;--teal:#00d4aa;--dim:#8892a6;--warn:#ffd700;"
"--txt:#e8edf5;--line:#232d47}"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:var(--bg);color:var(--txt);font-family:system-ui,sans-serif;"
"padding:16px;max-width:520px;margin:0 auto}"
"h1{font-size:17px;color:var(--teal);margin-bottom:2px}"
".sub{color:var(--dim);font-size:12px;margin-bottom:14px}"
".card{background:var(--card);border-radius:10px;padding:14px;margin-bottom:12px}"
".card b{font-size:14px}.k{color:var(--dim);font-size:12px;margin-top:6px}"
".v{font-size:22px;font-weight:700}.on{color:var(--teal)}.off{color:var(--dim)}"
"button{background:var(--teal);color:#04211a;border:0;border-radius:8px;"
"padding:10px 14px;font-size:14px;font-weight:600;margin:4px 6px 4px 0;cursor:pointer}"
"button.arm{background:transparent;color:var(--warn);border:1px solid var(--warn)}"
"button.arm.armed{background:var(--warn);color:#332600}"
"button.ap{display:flex;justify-content:space-between;width:100%;background:#101830;"
"color:var(--txt);border:1px solid var(--line);font-weight:400;margin:3px 0}"
".ap .r{color:var(--dim)}"
"input{background:#101830;border:1px solid var(--line);border-radius:8px;"
"color:var(--txt);padding:9px 10px;font-size:14px;margin:4px 0}"
"#ssid{width:46%}#pass{width:38%}"
".msg{color:var(--dim);font-size:12px;margin-top:6px;min-height:16px}"
".bar{background:rgba(255,215,0,.10);border:1px solid rgba(255,215,0,.35);"
"color:var(--warn);border-radius:8px;padding:8px 12px;font-size:13px;"
"margin-bottom:10px;display:none}"
"</style></head><body>"
"<h1>WiFi CSI 感知节点</h1>"
"<div class=\"sub\">板端设置页 —— 手机连同一 WiFi 即可在房间外操作 · 固件 v" APP_FW_VERSION "</div>"
"<div class=\"bar\" id=\"armbar\"></div>"
"<div class=\"card\"><b>当前状态</b>"
"<div class=\"v\" id=\"present\">—</div>"
"<div class=\"k\" id=\"detail\">加载中…</div>"
"<div class=\"k\" id=\"net\">·</div></div>"
"<div class=\"card\"><b>校准（房间需无人）</b><div class=\"k\">换房间/挪动设备/误报漏报后重学 30 秒无人基线</div>"
"<div style=\"margin-top:8px\">"
"<button onclick=\"cal(0)\">立即校准（30s）</button>"
"<button class=\"arm\" id=\"armbtn\" onclick=\"armCal()\">离开后校准（60s）</button>"
"</div><div class=\"msg\" id=\"calmsg\"></div></div>"
"<div class=\"card\"><b>WiFi 配网</b>"
"<button onclick=\"scan()\">扫描附近 WiFi</button>"
"<div id=\"list\"></div>"
"<input id=\"ssid\" placeholder=\"网络名称\">"
"<input id=\"pass\" type=\"password\" placeholder=\"密码\">"
"<button onclick=\"conn()\">连接</button>"
"<div class=\"msg\">连接瞬间感知会短暂中断，属正常</div></div>"
"<div class=\"card\"><b>固件更新（OTA）</b>"
"<input type=\"file\" id=\"file\" accept='.bin' style=\"width:100%\">"
"<button onclick=\"otaStart()\">上传并刷写</button>"
"<div class=\"bar\" id=\"otabar\" style=\"display:none\"></div>"
"<div class=\"msg\" id=\"otamsg\">校验失败不切槽，原固件无损</div></div>"
"<div class=\"card\"><b>维护</b><button onclick=\"reboot()\">重启设备</button></div>"
"<script>"
"const $=i=>document.getElementById(i);"
"async function post(u,b){const r=await fetch(u,{method:'POST',"
"headers:{'Content-Type':'application/json'},body:JSON.stringify(b||{})});"
"return r.json()}"
"function cal(d){post('/api/calibrate',{delay_s:d}).then(j=>msg(j.msg))}"
"function armCal(){post('/api/calibrate',$('armbtn').classList.contains('armed')?{cancel:true}:{delay_s:60})}"
"function msg(m){$('calmsg').textContent=m||''}"
"function scan(){$('list').innerHTML='';msg('扫描中…约 2 秒');"
"post('/api/wifi/scan').then(j=>{if(j.status!=='ok'){msg('失败: '+j.error);return}"
"j.data.sort((a,b)=>b.rssi-a.rssi).forEach(a=>{const b=document.createElement('button');"
"b.className='ap';"
"b.innerHTML='<span>'+a.ssid+(a.sec?' 🔒':'')+'</span><span class=\"r\">'+a.rssi+'</span>';"
"b.onclick=()=>{$('ssid').value=a.ssid;$('pass').focus()};"
"$('list').appendChild(b)});msg((j.data.length)+' 个网络')})}"
"function conn(){const s=$('ssid').value.trim(),p=$('pass').value;"
"if(!s){msg('请填网络名称');return}"
"post('/api/wifi/connect',{ssid:s,pass:p}).then(j=>msg(j.status==='ok'?'已下发连接 '+s+'，稍后自动恢复感知':'失败: '+j.error))}"
"async function tick(){try{const s=await (await fetch('/api/status')).json();"
"const p=s.present,armed=s.calib_pending_ms>0;"
"$('present').textContent=s.streaming?(p?'有人':'无人'):(s.streaming===false?'串流已暂停':'等待');"
"$('present').className='v '+(p?'on':'off');"
"$('detail').textContent='动作 '+(s.motion*100|0)+'% · 呼吸 '+(s.bpm>0?s.bpm+' bpm':'—')+' · 采样 '+(s.rate_hz||0).toFixed(1)+' Hz · '+s.state;"
"$('net').textContent=(s.ssid||'未连 WiFi')+' · '+(s.ip||'')+' · '+(s.rssi||0)+' dBm';"
"if(armed){const sec=Math.ceil(s.calib_pending_ms/1000);"
"$('armbar').style.display='block';"
"$('armbar').textContent='⏳ '+sec+' 秒后自动开始校准 —— 请离开房间';"
"$('armbtn').textContent='取消（'+sec+'s）';$('armbtn').classList.add('armed')}"
"else{$('armbar').style.display='none';"
"$('armbtn').textContent='离开后校准（60s）';$('armbtn').classList.remove('armed')}}catch(e){}}"
"function otaStart(){const f=$('file').files[0];if(!f){$('otamsg').textContent='先选 .bin 固件文件';return}"
"const x=new XMLHttpRequest();x.open('POST','/ota');"
"x.upload.onprogress=e=>{$('otabar').style.display='block';"
"$('otabar').textContent='上传 '+Math.round(100*e.loaded/e.total)+'%（'+e.loaded+'/'+e.total+' 字节）'};"
"x.onload=()=>{if(x.status==200){$('otabar').textContent='✅ 写入并通过校验，重启中…';"
"setTimeout(()=>location.reload(),15000)}"
"else{$('otabar').textContent='❌ HTTP '+x.status;"
"$('otamsg').textContent=x.responseText}}"
"x.onerror=()=>{$('otabar').textContent='⚠ 网络错误（设备可能已在重启）'};"
"$('otamsg').textContent='上传 '+f.size+' 字节…';x.send(f)}"
"async function reboot(){if(!confirm('确认重启？'))return;"
"try{await fetch('/api/reboot',{method:'POST'})}catch(e){}"
"setTimeout(()=>location.reload(),8000)}"
"tick();setInterval(tick,2000);"
"</script></body></html>";

/* ---------- 工具 ---------- */
static int read_body(httpd_req_t *req, char *buf, size_t cap)
{
    int total = 0, n;
    while (total < (int)cap - 1 &&
           (n = httpd_req_recv(req, buf + total, (size_t)((int)cap - 1 - total))) > 0) {
        total += n;
    }
    if (total < 0) {
        return -1;
    }
    buf[total] = '\0';
    return total;
}

static void send_json(httpd_req_t *req, cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
    cJSON_free(s);
}

/* ---------- 处理器 ---------- */
static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_status(httpd_req_t *req)
{
    sense_status_t st;
    app_sense_get_status(&st);
    uint32_t csi_total = 0, qdrop = 0;
    app_sense_get_diag(&csi_total, &qdrop);

    char ip[16] = "";
    app_wifi_get_ip_str(ip, sizeof(ip));
    const char *ssid = app_wifi_get_ssid();

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "state", st.calibrating ? "calibrating"
                                 : st.has_data    ? "tracking"
                                                  : "idle");
    cJSON_AddBoolToObject(j, "present", st.present);
    cJSON_AddNumberToObject(j, "motion", st.motion);
    cJSON_AddNumberToObject(j, "bpm", st.bpm);
    cJSON_AddNumberToObject(j, "quality", st.quality);
    cJSON_AddNumberToObject(j, "rate_hz", st.rate_hz);
    cJSON_AddNumberToObject(j, "rssi", app_wifi_get_rssi());
    cJSON_AddBoolToObject(j, "streaming", st.streaming);
    cJSON_AddBoolToObject(j, "wifi_connected", app_wifi_is_connected());
    cJSON_AddStringToObject(j, "ssid", ssid ? ssid : "");
    cJSON_AddStringToObject(j, "ip", ip);
    cJSON_AddStringToObject(j, "fw", APP_FW_VERSION);
    cJSON_AddNumberToObject(j, "calib_pending_ms", (double)app_sense_calib_pending_ms());
    cJSON_AddNumberToObject(j, "csi_total", (double)csi_total);
    cJSON_AddNumberToObject(j, "qdrop", (double)qdrop);
    send_json(req, j);
    cJSON_Delete(j);
    return ESP_OK;
}

static esp_err_t h_calibrate(httpd_req_t *req)
{
    char buf[128];
    read_body(req, buf, sizeof(buf));
    cJSON *body = cJSON_Parse(buf);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    char msg[96] = "";
    cJSON *cancel = body ? cJSON_GetObjectItem(body, "cancel") : NULL;
    cJSON *delay = body ? cJSON_GetObjectItem(body, "delay_s") : NULL;
    if (cJSON_IsTrue(cancel)) {
        app_sense_cancel_calibrate();
        snprintf(msg, sizeof(msg), "已取消布防");
    } else if (cJSON_IsNumber(delay) && delay->valuedouble > 0) {
        int s = (int)delay->valuedouble;
        if (s > 3600) s = 3600;
        app_sense_arm_calibrate(s);
        snprintf(msg, sizeof(msg), "已布防：%d 秒后自动校准，请离开房间", s);
    } else {
        app_sense_request_recalibrate();
        snprintf(msg, sizeof(msg), "已开始重新校准（30 秒无人基线）");
    }
    cJSON_AddStringToObject(resp, "msg", msg);
    cJSON_Delete(body);
    send_json(req, resp);
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t h_wifi_scan(httpd_req_t *req)
{
    app_wifi_scan(); // 阻塞式（~2s），期间 CSI 停更属预期
    cJSON *resp = cJSON_CreateObject();
    cJSON *aps = cJSON_CreateArray();
    int n = app_wifi_get_scan_count();
    for (int i = 0; i < n; i++) {
        wifi_ap_record_t *ap = app_wifi_get_scan_result(i);
        if (!ap) continue;
        cJSON *o = cJSON_CreateObject();
        char ssid[33] = "";
        memcpy(ssid, ap->ssid, sizeof(ap->ssid));
        cJSON_AddStringToObject(o, "ssid", ssid);
        cJSON_AddNumberToObject(o, "rssi", ap->rssi);
        cJSON_AddBoolToObject(o, "sec", ap->authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(aps, o);
    }
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddItemToObject(resp, "data", aps);
    send_json(req, resp);
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t h_wifi_connect(httpd_req_t *req)
{
    char buf[160];
    read_body(req, buf, sizeof(buf));
    cJSON *body = cJSON_Parse(buf);
    cJSON *resp = cJSON_CreateObject();
    const char *ssid = "", *pass = "";
    if (body) {
        cJSON *s = cJSON_GetObjectItem(body, "ssid");
        cJSON *p = cJSON_GetObjectItem(body, "pass");
        if (cJSON_IsString(s)) ssid = s->valuestring;
        if (cJSON_IsString(p)) pass = p->valuestring;
    }
    if (ssid[0] == '\0') {
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "error", "ssid 不能为空");
    } else {
        app_wifi_connect(ssid, pass);
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddStringToObject(resp, "msg", "连接指令已下发，关联后感知自动恢复");
    }
    cJSON_Delete(body);
    send_json(req, resp);
    cJSON_Delete(resp);
    return ESP_OK;
}

/* ---------- OTA 刷机 / 重启（与 env-station app_web 同构，基线能力） ---------- */

/* 延迟重启：先把 HTTP 应答发完再 esp_restart */
static void reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void schedule_reboot(int ms)
{
    static esp_timer_handle_t t;
    if (!t) {
        const esp_timer_create_args_t cfg = {
            .callback = reboot_cb,
            .name = "webreboot",
        };
        esp_timer_create(&cfg, &t);
    }
    esp_timer_stop(t);
    esp_timer_start_once(t, (uint64_t)ms * 1000);
}

static esp_err_t h_reboot(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    schedule_reboot(500);
    return ESP_OK;
}

static esp_err_t h_ota(httpd_req_t *req)
{
    static char buf[4096]; /* 静态收包缓冲（httpd 栈 4K，勿放大栈缓冲） */
    if (req->content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"empty body\"}",
                               HTTPD_RESP_USE_STRLEN);
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"no ota partition\"}",
                               HTTPD_RESP_USE_STRLEN);
    }
    esp_ota_handle_t ota;
    if (esp_ota_begin(part, (size_t)req->content_len, &ota) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"ota begin failed\"}",
                               HTTPD_RESP_USE_STRLEN);
    }
    ESP_LOGI(TAG, "OTA 开始：目标槽 %s，%u 字节", part->label,
             (unsigned)req->content_len);
    size_t remain = (size_t)req->content_len;
    while (remain > 0) {
        int got = httpd_req_recv(req, buf, remain > sizeof(buf) ? sizeof(buf) : remain);
        if (got <= 0) {
            esp_ota_abort(ota);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"recv failed\"}",
                                   HTTPD_RESP_USE_STRLEN);
        }
        if (esp_ota_write(ota, buf, (size_t)got) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"ota write failed\"}",
                                   HTTPD_RESP_USE_STRLEN);
        }
        remain -= (size_t)got;
    }
    /* end 做镜像头/哈希校验，失败不切启动槽——原固件无损 */
    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"image invalid\"}",
                               HTTPD_RESP_USE_STRLEN);
    }
    ESP_LOGI(TAG, "OTA 写入并通过校验，1s 后重启进新固件");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"msg\":\"done, rebooting\"}",
                    HTTPD_RESP_USE_STRLEN);
    schedule_reboot(1000);
    return ESP_OK;
}

/* ---------- 启动 ---------- */
void app_web_init(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 4096;
    cfg.max_uri_handlers = 8;
    /* lru_purge：满员时回收最久未活动会话。路由器每 ~5s 探测 :80 的僵尸
     * 连接（建立后不发数据）会永久占住 lwIP 槽直至耗尽（env-station 实测
     * 根因），两板同修（板间一致性）。 */
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Web 服务启动失败");
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_root },
        { .uri = "/api/status", .method = HTTP_GET, .handler = h_status },
        { .uri = "/api/calibrate", .method = HTTP_POST, .handler = h_calibrate },
        { .uri = "/api/wifi/scan", .method = HTTP_POST, .handler = h_wifi_scan },
        { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = h_wifi_connect },
        { .uri = "/ota", .method = HTTP_POST, .handler = h_ota },
        { .uri = "/api/reboot", .method = HTTP_POST, .handler = h_reboot },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }
    ESP_LOGI(TAG, "板端 Web 已启动：http://<本机IP>/ （IP 见 LCD Node 页）");
}
