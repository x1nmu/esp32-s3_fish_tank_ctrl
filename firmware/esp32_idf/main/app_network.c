#include "app_network.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "app_config.h"
#include "app_core.h"

static const char *TAG = "app_network";

static httpd_handle_t s_http_server;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static bool s_wifi_initialized;
static bool s_wifi_connect_pending;
static uint32_t s_wifi_connect_started_ms;
static uint32_t s_wifi_reconnect_not_before_ms;

static esp_err_t redirect_root(httpd_req_t *req);

static bool runtime_try_lock(TickType_t wait_ticks)
{
    return app_core_lock(wait_ticks);
}

static void runtime_unlock(void)
{
    app_core_unlock();
}

static void url_decode(char *text)
{
    char *src = text;
    char *dst = text;

    while (*src != '\0') {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool form_get_value(const char *body, const char *key, char *buffer, size_t size)
{
    const char *found = strstr(body, key);
    const char *end;
    size_t len;

    if (found == NULL) {
        return false;
    }

    found += strlen(key);
    end = strchr(found, '&');
    len = end == NULL ? strlen(found) : (size_t)(end - found);
    if (len >= size) {
        len = size - 1U;
    }
    memcpy(buffer, found, len);
    buffer[len] = '\0';
    url_decode(buffer);
    return true;
}

static void append_text(char *buffer, size_t size, size_t *offset, const char *text)
{
    if (*offset >= size) {
        return;
    }
    *offset += (size_t)snprintf(buffer + *offset, size - *offset, "%s", text);
}

static void append_fmt(char *buffer, size_t size, size_t *offset, const char *fmt, ...)
{
    va_list args;

    if (*offset >= size) {
        return;
    }

    va_start(args, fmt);
    *offset += (size_t)vsnprintf(buffer + *offset, size - *offset, fmt, args);
    va_end(args);
}

static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t w = 0;

    if (dst_size == 0) {
        return;
    }

    while (*src != '\0' && w + 1 < dst_size) {
        char c = *src++;

        if ((c == '"' || c == '\\') && w + 2 < dst_size) {
            dst[w++] = '\\';
            dst[w++] = c;
        } else if (c == '\n' && w + 2 < dst_size) {
            dst[w++] = '\\';
            dst[w++] = 'n';
        } else if (c == '\r' && w + 2 < dst_size) {
            dst[w++] = '\\';
            dst[w++] = 'r';
        } else if (c == '\t' && w + 2 < dst_size) {
            dst[w++] = '\\';
            dst[w++] = 't';
        } else {
            dst[w++] = c;
        }
    }

    dst[w] = '\0';
}

static void build_status_json(char *buffer, size_t size)
{
    char control_temp[16];
    char sensor_a[16];
    char sensor_b[16];
    char state_text[24];
    char fault_code_text[24];
    char fault_desc_text[96];
    char wifi_label[APP_WIFI_AP_SSID_MAX_LEN * 2];
    char wifi_ip[APP_IP_TEXT_MAX_LEN * 2];

    app_format_float(control_temp, sizeof(control_temp), g_runtime.control_temp, 2);
    app_format_float(sensor_a, sizeof(sensor_a), g_runtime.sensor_a.temperature, 2);
    app_format_float(sensor_b, sizeof(sensor_b), g_runtime.sensor_b.temperature, 2);
    json_escape(app_state_to_text(g_runtime.state), state_text, sizeof(state_text));
    json_escape(app_fault_code_to_text(g_runtime.last_fault_code), fault_code_text, sizeof(fault_code_text));
    json_escape(app_fault_code_to_description(g_runtime.last_fault_code), fault_desc_text, sizeof(fault_desc_text));
    json_escape(app_network_current_wifi_label(), wifi_label, sizeof(wifi_label));
    json_escape(app_network_current_ip_address(), wifi_ip, sizeof(wifi_ip));

    snprintf(buffer,
             size,
             "{\"state\":\"%s\",\"faultCode\":\"%s\",\"faultDescription\":\"%s\",\"faultLatched\":%s,"
             "\"setpoint\":%.1f,\"hysteresis\":%.1f,\"sensorDiffAlarm\":%.1f,\"controlTemp\":%s,\"sensorA\":%s,\"sensorB\":%s,"
             "\"heatOn\":%s,\"coolOn\":%s,\"sensorMismatch\":%s,\"sensorAFailed\":%s,\"sensorBFailed\":%s,"
             "\"lowTempCutoff\":%s,\"highTempCutoff\":%s,\"compressorProtected\":%s,\"startupInhibit\":%s,"
             "\"wifiApMode\":%s,\"wifiConnected\":%s,\"wifiSsid\":\"%s\",\"wifiIp\":\"%s\",\"buzzerEnabled\":%s,"
             "\"totalHeatOnMs\":%lu,\"totalCoolOnMs\":%lu,\"alarmCount\":%u,\"degradedCount\":%u,\"faultStopCount\":%u,"
             "\"heatRelaySwitchCount\":%lu,\"coolRelaySwitchCount\":%lu,\"apClientCount\":%u}",
             state_text,
             fault_code_text,
             fault_desc_text,
             g_runtime.fault_latched ? "true" : "false",
             g_runtime.setpoint,
             g_runtime.hysteresis,
             g_runtime.sensor_diff_alarm,
             isnan(g_runtime.control_temp) ? "null" : control_temp,
             isnan(g_runtime.sensor_a.temperature) ? "null" : sensor_a,
             isnan(g_runtime.sensor_b.temperature) ? "null" : sensor_b,
             g_runtime.heat_on ? "true" : "false",
             g_runtime.cool_on ? "true" : "false",
             g_runtime.alarms.sensor_mismatch ? "true" : "false",
             g_runtime.alarms.sensor_a_failed ? "true" : "false",
             g_runtime.alarms.sensor_b_failed ? "true" : "false",
             g_runtime.alarms.low_temp_cutoff ? "true" : "false",
             g_runtime.alarms.high_temp_cutoff ? "true" : "false",
             g_runtime.alarms.compressor_protected ? "true" : "false",
             g_runtime.alarms.output_startup_inhibit ? "true" : "false",
             g_runtime.wifi_ap_mode ? "true" : "false",
             g_runtime.wifi_connected ? "true" : "false",
             wifi_label,
             wifi_ip,
             g_runtime.buzzer_enabled ? "true" : "false",
             (unsigned long)g_runtime.total_heat_on_ms,
             (unsigned long)g_runtime.total_cool_on_ms,
             g_runtime.alarm_count,
             g_runtime.degraded_count,
             g_runtime.fault_stop_count,
             (unsigned long)g_runtime.heat_relay_switch_count,
             (unsigned long)g_runtime.cool_relay_switch_count,
             app_network_ap_client_count());
}

static const char *http_status_text(int status_code)
{
    switch (status_code) {
        case 200:
            return "200 OK";
        case 400:
            return "400 Bad Request";
        case 500:
            return "500 Internal Server Error";
        default:
            return "200 OK";
    }
}

static bool request_wants_json(httpd_req_t *req)
{
    int query_len = httpd_req_get_url_query_len(req);
    char query[64];
    char ajax[8];

    if (query_len <= 0 || query_len >= (int)sizeof(query)) {
        return false;
    }
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(query, "ajax", ajax, sizeof(ajax)) != ESP_OK) {
        return false;
    }
    return strcmp(ajax, "1") == 0;
}

static esp_err_t send_action_response(httpd_req_t *req, int status_code, const char *message, bool is_error)
{
    char body[256];

    if (request_wants_json(req)) {
        snprintf(body,
                 sizeof(body),
                 "{\"ok\":%s,\"message\":\"%s\"}",
                 is_error ? "false" : "true",
                 message != NULL ? message : "");
        httpd_resp_set_status(req, http_status_text(status_code));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    }

    if (is_error) {
        return httpd_resp_send_err(req, status_code == 400 ? HTTPD_400_BAD_REQUEST : HTTPD_500_INTERNAL_SERVER_ERROR, message);
    }

    return redirect_root(req);
}

static void build_root_page(char *buffer, size_t size)
{
    char temp_buf[16];
    size_t offset = 0;

    append_text(buffer, size, &offset, "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    append_text(buffer, size, &offset, "<title>Fish Tank Controller</title>");
    append_text(buffer, size, &offset, "<style>body{font-family:Segoe UI,Arial,sans-serif;background:#eef6f7;color:#102a43;padding:20px;}"
                                     ".card{max-width:680px;margin:auto;background:#fff;border-radius:16px;padding:20px;box-shadow:0 10px 30px rgba(16,42,67,.12);}"
                                     ".grid{display:grid;grid-template-columns:repeat(2,minmax(140px,1fr));gap:12px;}"
                                     ".item{background:#f0f4f8;border-radius:12px;padding:12px;}small{display:block;color:#627d98;}strong{font-size:18px;}"
                                     ".notice{margin:14px 0;padding:12px;border-radius:12px;background:#fff3cd;color:#7c5700;}"
                                     ".entry{margin:12px 0;padding:12px;border-radius:12px;background:#e6fffa;color:#0f5132;border:1px solid #b7efe2;}"
                                     ".entry code{font-family:Consolas,monospace;background:#f5f7fa;padding:1px 4px;border-radius:4px;}"
                                     ".notice-success{background:#d9fbe8;color:#166534;}"
                                     ".notice-error{background:#fde8e8;color:#991b1b;}"
                                     "form{margin-top:16px;padding:16px;background:#f8fbfc;border-radius:12px;}label{display:block;margin:8px 0 6px;}input[type='text'],input[type='password'],input[type='number']{width:100%;padding:10px;border:1px solid #bcccdc;border-radius:10px;box-sizing:border-box;}button{margin-top:12px;padding:10px 16px;border:0;border-radius:10px;background:#0f766e;color:#fff;}details{margin-top:12px;padding:10px;border:1px solid #d9e2ec;border-radius:10px;background:#fff;}summary{cursor:pointer;font-weight:600;color:#334e68;}.checkbox-label{display:flex;align-items:center;gap:8px;margin-top:10px;}.checkbox-label input{width:auto;margin:0;}h2{margin-bottom:8px;}</style></head><body><div class='card'>");
    append_text(buffer, size, &offset, "<h1>100L 鱼缸温控器 ESP-IDF</h1>");
    append_fmt(buffer,
               size,
               &offset,
               "<div class='entry'><strong>访问入口</strong><br>"
               "1) 与设备在同一网络，浏览器打开 <code>http://%s/</code><br>"
               "2) 若连不上，连接热点 <code>%s</code>（密码 <code>%s</code>）后打开 <code>http://192.168.4.1/</code>"
               "</div>",
               app_network_current_ip_address(),
               g_runtime.wifi_ap_ssid[0] != '\0' ? g_runtime.wifi_ap_ssid : APP_AP_SSID_PREFIX,
               APP_AP_PASSWORD);
    append_text(buffer, size, &offset, "<div class='grid'>");
    append_fmt(buffer, size, &offset, "<div class='item'><small>状态</small><strong id='status-state'>%s</strong></div>", app_state_to_text(g_runtime.state));
    append_fmt(buffer, size, &offset, "<div class='item'><small>故障</small><strong id='status-faultCode'>%s</strong></div>", g_runtime.last_fault_text);
    append_fmt(buffer, size, &offset, "<div class='item'><small>目标温度</small><strong id='status-setpoint'>%.1f C</strong></div>", g_runtime.setpoint);
    app_format_float(temp_buf, sizeof(temp_buf), g_runtime.control_temp, 2);
    append_fmt(buffer, size, &offset, "<div class='item'><small>控制温度</small><strong id='status-controlTemp'>%s C</strong></div>", temp_buf);
    append_fmt(buffer, size, &offset, "<div class='item'><small>加热</small><strong id='status-heatOn'>%s</strong></div>", g_runtime.heat_on ? "ON" : "OFF");
    append_fmt(buffer, size, &offset, "<div class='item'><small>制冷</small><strong id='status-coolOn'>%s</strong></div>", g_runtime.cool_on ? "ON" : "OFF");
    append_fmt(buffer, size, &offset, "<div class='item'><small>蜂鸣器</small><strong id='status-buzzer'>%s</strong></div>", g_runtime.buzzer_enabled ? "ON" : "OFF");
    append_fmt(buffer, size, &offset, "<div class='item'><small>网络</small><strong id='status-network'>%s / %s</strong></div>", app_network_current_wifi_label(), app_network_current_ip_address());
    append_text(buffer, size, &offset, "</div>");
    append_text(buffer, size, &offset, "<div id='action-notice'></div>");
    append_fmt(buffer,
               size,
               &offset,
               "<form id='control-settings-form' method='post' action='/settings/control'><h2>目标温度</h2><label>设定值</label><input name='setpoint' type='number' min='20' max='32' step='0.1' value='%.1f'><details><summary>高级设置</summary><label>回差</label><input name='hysteresis' type='number' min='0.3' max='2.0' step='0.1' value='%.1f'><label>探头差值告警阈值</label><input name='sensorDiffAlarm' type='number' min='0.3' max='3.0' step='0.1' value='%.1f'><label class='checkbox-label'><input name='buzzerEnabled' type='checkbox' value='1'%s><span>启用蜂鸣器</span></label></details><button type='submit'>保存控制参数</button></form>",
               g_runtime.setpoint,
               g_runtime.hysteresis,
               g_runtime.sensor_diff_alarm,
               g_runtime.buzzer_enabled ? " checked" : "");
    append_fmt(buffer, size, &offset, "<form id='wifi-settings-form' method='post' action='/settings/wifi'><h2>WiFi 设置</h2><label>WiFi SSID</label><input name='wifiSsid' type='text' maxlength='32' value='%s'>", g_runtime.wifi_ssid);
    append_text(buffer, size, &offset, "<label>WiFi 密码</label><input name='wifiPassword' type='password' maxlength='64' value='' placeholder='留空表示不修改密码'>");
    append_text(buffer, size, &offset, "<button type='submit'>保存 WiFi 并重连</button></form>");
    append_text(buffer, size, &offset, "<form id='fault-reset-form' method='post' action='/faults/reset'><h2>故障复位</h2><button type='submit'>清除锁定故障</button></form>");
    append_text(buffer, size, &offset, "<form id='stats-reset-form' method='post' action='/stats/reset'><h2>统计清零</h2><button type='submit'>清零累计统计</button></form>");
    append_text(buffer, size, &offset,
                "<script>"
                "function fmtTemp(v,d){if(v===null||v===undefined||Number.isNaN(v)){return '--';}return Number(v).toFixed(d);}"
                "function boolText(v){return v?'ON':'OFF';}"
                "function showNotice(msg,isErr){const host=document.getElementById('action-notice');if(!host)return;if(!msg){host.innerHTML='';return;}host.innerHTML='<div class=\\'notice '+(isErr?'notice-error':'notice-success')+'\\'>'+msg+'</div>';window.scrollTo({top:0,behavior:'smooth'});}"
                "function setText(id,val){const el=document.getElementById(id);if(el){el.textContent=val;}}"
                "let reconnectTimer=null;let reconnectTry=0;"
                "async function refreshStatus(){try{const r=await fetch('/status',{cache:'no-store'});if(!r.ok)return null;const s=await r.json();setText('status-state',s.state||'--');setText('status-faultCode',s.faultCode||'--');setText('status-setpoint',(s.setpoint!=null?Number(s.setpoint).toFixed(1):'--')+' C');setText('status-controlTemp',fmtTemp(s.controlTemp,2)+' C');setText('status-heatOn',boolText(!!s.heatOn));setText('status-coolOn',boolText(!!s.coolOn));setText('status-buzzer',boolText(!!s.buzzerEnabled));setText('status-network',(s.wifiSsid||'--')+' / '+(s.wifiIp||'--'));return s;}catch(e){console.log(e);return null;}}"
                "function startWifiReconnectMonitor(){if(reconnectTimer){clearInterval(reconnectTimer);}reconnectTry=0;reconnectTimer=setInterval(async()=>{reconnectTry++;const s=await refreshStatus();if(s&&(s.wifiConnected||s.wifiApMode)){clearInterval(reconnectTimer);reconnectTimer=null;showNotice('网络已更新，页面即将刷新。',false);setTimeout(()=>{window.location.reload();},800);return;}if(reconnectTry>=20){clearInterval(reconnectTimer);reconnectTimer=null;showNotice('仍在重连，请按页面入口提示重新访问。',true);}},1000);}"
                "async function submitFormAjax(form){const data=new URLSearchParams(new FormData(form));const url=form.action+(form.action.includes('?')?'&':'?')+'ajax=1';try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:data.toString()});let msg='操作成功';let ok=r.ok;try{const j=await r.json();if(j&&j.message){msg=j.message;}if(j&&j.ok===false){ok=false;}}catch(_){}showNotice(msg,!ok);if(ok){await refreshStatus();if(form.id==='wifi-settings-form'){showNotice(msg+' 设备正在重连WiFi，请稍候...',false);startWifiReconnectMonitor();}}}catch(e){showNotice('网络异常，请重试',true);}}"
                "['control-settings-form','wifi-settings-form','fault-reset-form','stats-reset-form'].forEach(id=>{const f=document.getElementById(id);if(f){f.addEventListener('submit',function(ev){ev.preventDefault();submitFormAjax(f);});}});"
                "refreshStatus();setInterval(()=>{refreshStatus();},5000);"
                "</script>");
    append_text(buffer, size, &offset, "</div></body></html>");
}

static esp_err_t redirect_root(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char *html = (char *)calloc(1, 12288);
    esp_err_t err;

    if (html == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "alloc failed");
    }

    if (!runtime_try_lock(pdMS_TO_TICKS(200))) {
        free(html);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime busy");
    }
    build_root_page(html, 12288);
    runtime_unlock();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    err = httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return err;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char json[2048];

    if (!runtime_try_lock(pdMS_TO_TICKS(100))) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime busy");
    }
    build_status_json(json, sizeof(json));
    runtime_unlock();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_control_post_handler(httpd_req_t *req)
{
    char body[512] = {0};
    char value[128];
    app_command_t cmd = {0};
    runtime_data_t snap;
    int received = httpd_req_recv(req, body, req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1);

    if (received <= 0) {
        return send_action_response(req, 400, "请求体为空，未接收到设置参数。", true);
    }

    app_core_get_runtime_snapshot(&snap);
    cmd.type = APP_CMD_UPDATE_SETTINGS;
    cmd.setpoint = snap.setpoint;
    cmd.hysteresis = snap.hysteresis;
    cmd.sensor_diff_alarm = snap.sensor_diff_alarm;
    cmd.buzzer_enabled = snap.buzzer_enabled;
    cmd.update_wifi = false;

    if (form_get_value(body, "setpoint=", value, sizeof(value))) {
        float new_setpoint = strtof(value, NULL);
        if (new_setpoint < APP_MIN_SETPOINT || new_setpoint > APP_MAX_SETPOINT) {
            return send_action_response(req, 400, "目标温度超出范围，应在 20.0 到 32.0 之间。", true);
        }
        cmd.setpoint = new_setpoint;
    }
    if (form_get_value(body, "hysteresis=", value, sizeof(value))) {
        float new_hysteresis = strtof(value, NULL);
        if (new_hysteresis < APP_MIN_HYSTERESIS || new_hysteresis > APP_MAX_HYSTERESIS) {
            return send_action_response(req, 400, "回差超出范围，应在 0.3 到 2.0 之间。", true);
        }
        cmd.hysteresis = new_hysteresis;
    }
    if (form_get_value(body, "sensorDiffAlarm=", value, sizeof(value))) {
        float new_diff_alarm = strtof(value, NULL);
        if (new_diff_alarm < APP_MIN_SENSOR_DIFF_ALARM || new_diff_alarm > APP_MAX_SENSOR_DIFF_ALARM) {
            return send_action_response(req, 400, "探头差值告警阈值超出范围，应在 0.3 到 3.0 之间。", true);
        }
        cmd.sensor_diff_alarm = new_diff_alarm;
    }

    if (strstr(body, "buzzerEnabled=") != NULL) {
        cmd.buzzer_enabled = strstr(body, "buzzerEnabled=1") != NULL;
    }

    if (app_core_post_command(&cmd, pdMS_TO_TICKS(100)) != ESP_OK) {
        return send_action_response(req, 500, "系统忙，请稍后重试。", true);
    }
    return send_action_response(req, 200, "控制参数已保存，页面数据已刷新。", false);
}

static esp_err_t settings_wifi_post_handler(httpd_req_t *req)
{
    char body[512] = {0};
    app_command_t cmd = {0};
    runtime_data_t snap;
    bool has_ssid = false;
    bool has_password_field = false;
    char wifi_password_input[APP_WIFI_PASSWORD_MAX_LEN] = {0};
    int received = httpd_req_recv(req, body, req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1);

    if (received <= 0) {
        return send_action_response(req, 400, "请求体为空，未接收到 WiFi 参数。", true);
    }

    app_core_get_runtime_snapshot(&snap);
    cmd.type = APP_CMD_UPDATE_SETTINGS;
    cmd.setpoint = snap.setpoint;
    cmd.hysteresis = snap.hysteresis;
    cmd.sensor_diff_alarm = snap.sensor_diff_alarm;
    cmd.buzzer_enabled = snap.buzzer_enabled;
    cmd.update_wifi = true;
    strlcpy(cmd.wifi_ssid, snap.wifi_ssid, sizeof(cmd.wifi_ssid));
    strlcpy(cmd.wifi_password, snap.wifi_password, sizeof(cmd.wifi_password));

    has_ssid = form_get_value(body, "wifiSsid=", cmd.wifi_ssid, sizeof(cmd.wifi_ssid));
    has_password_field = form_get_value(body, "wifiPassword=", wifi_password_input, sizeof(wifi_password_input));

    if (!has_ssid && !has_password_field) {
        return send_action_response(req, 400, "未收到 WiFi 参数，未执行保存。", true);
    }
    if (cmd.wifi_ssid[0] == '\0') {
        return send_action_response(req, 400, "WiFi SSID 不能为空。", true);
    }
    if (has_password_field && wifi_password_input[0] != '\0') {
        strlcpy(cmd.wifi_password, wifi_password_input, sizeof(cmd.wifi_password));
    }

    if (app_core_post_command(&cmd, pdMS_TO_TICKS(100)) != ESP_OK) {
        return send_action_response(req, 500, "系统忙，请稍后重试。", true);
    }
    s_wifi_reconnect_not_before_ms = app_millis() + APP_WIFI_SAVE_RECONNECT_DELAY_MS;
    return send_action_response(req, 200, "WiFi 设置已保存，系统将在约1秒后自动重连。", false);
}

static esp_err_t setpoint_post_handler(httpd_req_t *req)
{
    char body[64] = {0};
    char value[32];
    app_command_t cmd = {0};
    int received = httpd_req_recv(req, body, req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1);

    if (received <= 0 || !form_get_value(body, "value=", value, sizeof(value))) {
        return send_action_response(req, 400, "缺少 value 参数，无法更新目标温度。", true);
    }

    {
        float new_setpoint = strtof(value, NULL);
        if (new_setpoint < APP_MIN_SETPOINT || new_setpoint > APP_MAX_SETPOINT) {
            return send_action_response(req, 400, "目标温度超出范围，应在 20.0 到 32.0 之间。", true);
        }
        cmd.type = APP_CMD_SET_SETPOINT;
        cmd.setpoint = new_setpoint;
    }

    if (app_core_post_command(&cmd, pdMS_TO_TICKS(100)) != ESP_OK) {
        return send_action_response(req, 500, "系统忙，请稍后重试。", true);
    }
    return send_action_response(req, 200, "目标温度已更新。", false);
}

static esp_err_t fault_reset_post_handler(httpd_req_t *req)
{
    runtime_data_t snap;
    app_command_t cmd = { .type = APP_CMD_CLEAR_FAULT };

    app_core_get_runtime_snapshot(&snap);
    if (!snap.fault_latched) {
        return send_action_response(req, 200, "当前没有锁存故障，无需复位。", false);
    }

    if (app_core_post_command(&cmd, pdMS_TO_TICKS(100)) != ESP_OK) {
        return send_action_response(req, 500, "系统忙，请稍后重试。", true);
    }
    return send_action_response(req, 200, "锁存故障已清除。", false);
}

static esp_err_t stats_reset_post_handler(httpd_req_t *req)
{
    app_command_t cmd = { .type = APP_CMD_CLEAR_STATS };
    if (app_core_post_command(&cmd, pdMS_TO_TICKS(100)) != ESP_OK) {
        return send_action_response(req, 500, "系统忙，请稍后重试。", true);
    }
    return send_action_response(req, 200, "运行统计已清零。", false);
}

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
    httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL };
    httpd_uri_t setpoint = { .uri = "/set", .method = HTTP_POST, .handler = setpoint_post_handler, .user_ctx = NULL };
    httpd_uri_t settings = { .uri = "/settings", .method = HTTP_POST, .handler = settings_control_post_handler, .user_ctx = NULL };
    httpd_uri_t settings_control = { .uri = "/settings/control", .method = HTTP_POST, .handler = settings_control_post_handler, .user_ctx = NULL };
    httpd_uri_t settings_wifi = { .uri = "/settings/wifi", .method = HTTP_POST, .handler = settings_wifi_post_handler, .user_ctx = NULL };
    httpd_uri_t fault_reset = { .uri = "/faults/reset", .method = HTTP_POST, .handler = fault_reset_post_handler, .user_ctx = NULL };
    httpd_uri_t stats_reset = { .uri = "/stats/reset", .method = HTTP_POST, .handler = stats_reset_post_handler, .user_ctx = NULL };

    ESP_RETURN_ON_ERROR(httpd_start(&s_http_server, &config), TAG, "httpd start failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &root), TAG, "root handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &status), TAG, "status handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &setpoint), TAG, "setpoint handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &settings), TAG, "settings handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &settings_control), TAG, "settings control handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &settings_wifi), TAG, "settings wifi handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &fault_reset), TAG, "fault reset handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &stats_reset), TAG, "stats reset handler failed");
    app_log_event("Web server started, open http://%s/", app_network_current_ip_address());
    return ESP_OK;
}

static void update_ip_text_from_netif(esp_netif_t *netif)
{
    esp_netif_ip_info_t ip_info;

    if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(g_runtime.ip_text, sizeof(g_runtime.ip_text), IPSTR, IP2STR(&ip_info.ip));
    } else {
        strlcpy(g_runtime.ip_text, "--", sizeof(g_runtime.ip_text));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (!runtime_try_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "runtime busy in wifi_event_handler");
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_runtime.wifi_connected = false;
        if (!g_runtime.wifi_ap_mode) {
            strlcpy(g_runtime.ip_text, "--", sizeof(g_runtime.ip_text));
        }
        ESP_LOGW(TAG, "WiFi disconnected");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        g_runtime.wifi_connected = true;
        g_runtime.wifi_ap_mode = false;
        update_ip_text_from_netif(s_sta_netif);
        app_log_event("WiFi connected: %s", g_runtime.ip_text);
        app_log_event("Web entry: http://%s/", g_runtime.ip_text);
    }

    runtime_unlock();
}

static esp_err_t start_config_ap(void)
{
    wifi_config_t ap_config = {0};
    uint8_t mac[6];
    size_t ssid_len;

    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(g_runtime.wifi_ap_ssid,
             sizeof(g_runtime.wifi_ap_ssid),
             "%s-%02X%02X",
             APP_AP_SSID_PREFIX,
             mac[4],
             mac[5]);

    ssid_len = strlen(g_runtime.wifi_ap_ssid);
    memcpy(ap_config.ap.ssid, g_runtime.wifi_ap_ssid, ssid_len);
    memcpy(ap_config.ap.password, APP_AP_PASSWORD, strlen(APP_AP_PASSWORD));
    ap_config.ap.ssid_len = (uint8_t)ssid_len;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "wifi stop failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set AP config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP failed");

    if (!runtime_try_lock(pdMS_TO_TICKS(100))) {
        return ESP_ERR_TIMEOUT;
    }
    s_wifi_connect_pending = false;
    g_runtime.wifi_ap_mode = true;
    g_runtime.wifi_connected = false;
    g_runtime.wifi_ap_started_ms = app_millis();
    update_ip_text_from_netif(s_ap_netif);
    app_log_event("WiFi AP started: SSID=%s IP=%s", g_runtime.wifi_ap_ssid, g_runtime.ip_text);
    app_log_event("AP web entry: connect %s then open http://%s/", g_runtime.wifi_ap_ssid, g_runtime.ip_text);
    runtime_unlock();
    return ESP_OK;
}

esp_err_t app_network_connect_wifi(bool force_reconnect)
{
    wifi_config_t wifi_config = {0};
    char ssid[APP_WIFI_SSID_MAX_LEN];
    char password[APP_WIFI_PASSWORD_MAX_LEN];
    bool no_sta_credentials = false;

    if (!runtime_try_lock(pdMS_TO_TICKS(200))) {
        return ESP_ERR_TIMEOUT;
    }

    if (g_runtime.wifi_ssid[0] != '\0') {
        strlcpy(ssid, g_runtime.wifi_ssid, sizeof(ssid));
    } else {
        strlcpy(ssid, APP_WIFI_SSID, sizeof(ssid));
    }

    if (g_runtime.wifi_password[0] != '\0') {
        strlcpy(password, g_runtime.wifi_password, sizeof(password));
    } else {
        strlcpy(password, APP_WIFI_PASSWORD, sizeof(password));
    }

    if (!force_reconnect && g_runtime.wifi_connected && !g_runtime.wifi_ap_mode) {
        runtime_unlock();
        return ESP_OK;
    }

    if (ssid[0] == '\0') {
        no_sta_credentials = true;
    }

    g_runtime.wifi_reconnect_requested = false;
    g_runtime.last_wifi_retry_ms = app_millis();
    g_runtime.wifi_connected = false;
    g_runtime.wifi_ap_mode = false;
    g_runtime.wifi_ap_ssid[0] = '\0';
    strlcpy(g_runtime.ip_text, "--", sizeof(g_runtime.ip_text));
    s_wifi_connect_pending = false;
    runtime_unlock();

    if (no_sta_credentials) {
        app_log_event("No WiFi credentials, starting AP mode directly");
        return start_config_ap();
    }

    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "wifi stop failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set STA config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect failed");

    app_log_event("WiFi connecting to %s", ssid);
    s_wifi_connect_pending = true;
    s_wifi_connect_started_ms = app_millis();
    return ESP_OK;
}

const char *app_network_current_ip_address(void)
{
    return g_runtime.ip_text[0] != '\0' ? g_runtime.ip_text : "--";
}

const char *app_network_current_wifi_label(void)
{
    if (g_runtime.wifi_ap_mode) {
        return g_runtime.wifi_ap_ssid[0] != '\0' ? g_runtime.wifi_ap_ssid : "--";
    }
    if (g_runtime.wifi_connected) {
        return g_runtime.wifi_ssid[0] != '\0' ? g_runtime.wifi_ssid : APP_WIFI_SSID;
    }
    return g_runtime.wifi_ssid[0] != '\0' ? g_runtime.wifi_ssid : "--";
}

uint16_t app_network_ap_client_count(void)
{
    wifi_sta_list_t sta_list;

    if (!g_runtime.wifi_ap_mode) {
        return 0;
    }
    if (esp_wifi_ap_get_sta_list(&sta_list) != ESP_OK) {
        return 0;
    }
    return sta_list.num;
}

esp_err_t app_network_init(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    if (!s_wifi_initialized) {
        ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
        ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop create failed");
        s_sta_netif = esp_netif_create_default_wifi_sta();
        s_ap_netif = esp_netif_create_default_wifi_ap();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "wifi event register failed");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "ip event register failed");
        s_wifi_initialized = true;
    }

    if (s_http_server == NULL) {
        ESP_RETURN_ON_ERROR(start_web_server(), TAG, "web server start failed");
    }

    return app_network_connect_wifi(true);
}

void app_network_maintain(void)
{
    bool should_retry_sta;
    bool need_reconnect = false;
    bool wifi_connected = false;
    uint32_t connect_elapsed_ms;

    if (s_wifi_connect_pending) {
        if (runtime_try_lock(pdMS_TO_TICKS(20))) {
            wifi_connected = g_runtime.wifi_connected;
            runtime_unlock();
        }

        if (wifi_connected) {
            s_wifi_connect_pending = false;
            return;
        }

        connect_elapsed_ms = app_millis() - s_wifi_connect_started_ms;
        if (connect_elapsed_ms >= APP_WIFI_CONNECT_TIMEOUT_MS) {
            s_wifi_connect_pending = false;
            app_log_event("WiFi connection timeout, switching to AP mode");
            (void)start_config_ap();
        }
        return;
    }

    if (!runtime_try_lock(pdMS_TO_TICKS(100))) {
        return;
    }

    if (g_runtime.wifi_ap_mode) {
        should_retry_sta = g_runtime.wifi_ssid[0] != '\0' &&
                           app_network_ap_client_count() == 0 &&
                           (app_millis() - g_runtime.wifi_ap_started_ms) >= APP_WIFI_AP_RETRY_INTERVAL_MS;
        if (should_retry_sta) {
            app_log_event("AP idle timeout reached, retrying STA connection");
            need_reconnect = true;
        }
    } else if ((g_runtime.wifi_reconnect_requested && app_millis() >= s_wifi_reconnect_not_before_ms) ||
               (!g_runtime.wifi_connected && (app_millis() - g_runtime.last_wifi_retry_ms) >= APP_WIFI_RETRY_INTERVAL_MS)) {
        need_reconnect = true;
    }

    runtime_unlock();

    if (need_reconnect) {
        s_wifi_reconnect_not_before_ms = 0;
        (void)app_network_connect_wifi(true);
    }
}