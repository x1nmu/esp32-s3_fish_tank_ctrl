#include "app_network.h"
#include "app_runtime.h"

namespace {

String buildHtmlPage(const String& actionMessage = "", bool isError = false);

void copyStringToBuffer(const String& value, char* buffer, size_t bufferSize) {
  String trimmed = value;
  trimmed.trim();
  trimmed.toCharArray(buffer, bufferSize);
}

bool hasConfiguredWiFi() {
  return runtime.wifiSsid[0] != '\0';
}

String formatDurationMs(uint32_t durationMs) {
  uint32_t totalSeconds = durationMs / 1000;
  uint32_t hours = totalSeconds / 3600;
  uint32_t minutes = (totalSeconds % 3600) / 60;
  uint32_t seconds = totalSeconds % 60;

  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
  return String(buffer);
}

String buildPageNotice(const String& message, bool isError) {
  if (message.length() == 0) {
    return "";
  }

  String cssClass = isError ? "notice notice-error" : "notice notice-success";
  return "<div class='" + cssClass + "'>" + message + "</div>";
}

String escapeJsonString(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t index = 0; index < value.length(); ++index) {
    char ch = value[index];
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

bool requestWantsJson() {
  return server.hasArg("ajax") && server.arg("ajax") == "1";
}

void sendActionResponse(int statusCode, const String& message, bool isError) {
  if (requestWantsJson()) {
    String json = "{";
    json += "\"ok\":" + String(isError ? "false" : "true") + ",";
    json += "\"message\":\"" + escapeJsonString(message) + "\"";
    json += "}";
    server.send(statusCode, "application/json", json);
    return;
  }

  server.send(statusCode, "text/html; charset=utf-8", buildHtmlPage(message, isError));
}

String buildStatusItem(const String& id, const String& label, const String& value) {
  return "<div class='item'><small>" + label + "</small><strong id='" + id + "'>" + value + "</strong></div>";
}

String buildSystemNoticeHtml() {
  if (runtime.wifiApMode) {
    return "<div class='notice'>当前处于 AP 配网模式。请连接热点 <strong>" +
           String(runtime.wifiApSsid) +
           "</strong>，密码 <strong>" +
           String(ControlConfig::AP_PASSWORD) +
           "</strong>，然后在本页填写路由器账号。</div>";
  }
  if (runtime.wifiReconnectRequested) {
    return "<div class='notice'>WiFi 参数已保存，系统正在尝试重新连接路由器。</div>";
  }
  if (runtime.faultLatched) {
    return "<div class='notice'>系统处于硬保护锁定状态，请先检查温度和执行机构，再手动复位。</div>";
  }
  if (runtime.alarms.heatRelayWearWarning || runtime.alarms.coolRelayWearWarning) {
    return "<div class='notice'>继电器动作次数已接近维护阈值，建议检查触点寿命并预备更换。</div>";
  }
  String entry = "<div class='notice'>访问入口：";
  if (WiFi.status() == WL_CONNECTED) {
    entry += "同网访问 <strong>http://" + WiFi.localIP().toString() + "</strong>";
  } else {
    entry += "STA 未连接";
  }
  entry += "；AP 配网 <strong>http://192.168.4.1</strong></div>";
  return entry;
}

String buildJsonStatus() {
  String json = "{";
  json += "\"state\":\"" + escapeJsonString(stateToText(runtime.state)) + "\",";
  json += "\"faultCode\":\"" + escapeJsonString(faultCodeToText(runtime.lastFaultCode)) + "\",";
  json += "\"lastFaultText\":\"" + escapeJsonString(runtime.lastFaultText) + "\",";
  json += "\"faultDescription\":\"" + escapeJsonString(faultCodeToDescription(runtime.lastFaultCode)) + "\",";
  json += "\"faultLatched\":" + String(runtime.faultLatched ? "true" : "false") + ",";
  json += "\"setpoint\":" + String(runtime.setpoint, 1) + ",";
  json += "\"hysteresis\":" + String(runtime.hysteresis, 1) + ",";
  json += "\"sensorDiffAlarm\":" + String(runtime.sensorDiffAlarm, 1) + ",";
  json += "\"controlTemp\":" + (isnan(runtime.controlTemp) ? String("null") : String(runtime.controlTemp, 2)) + ",";
  json += "\"sensorA\":" + (isnan(runtime.sensorA.temperature) ? String("null") : String(runtime.sensorA.temperature, 2)) + ",";
  json += "\"sensorB\":" + (isnan(runtime.sensorB.temperature) ? String("null") : String(runtime.sensorB.temperature, 2)) + ",";
  json += "\"heatOn\":" + String(runtime.heatOn ? "true" : "false") + ",";
  json += "\"coolOn\":" + String(runtime.coolOn ? "true" : "false") + ",";
  json += "\"sensorMismatch\":" + String(runtime.alarms.sensorMismatch ? "true" : "false") + ",";
  json += "\"sensorAFailed\":" + String(runtime.alarms.sensorAFailed ? "true" : "false") + ",";
  json += "\"sensorBFailed\":" + String(runtime.alarms.sensorBFailed ? "true" : "false") + ",";
  json += "\"lowTempCutoff\":" + String(runtime.alarms.lowTempCutoff ? "true" : "false") + ",";
  json += "\"highTempCutoff\":" + String(runtime.alarms.highTempCutoff ? "true" : "false") + ",";
  json += "\"heatRelayWearWarning\":" + String(runtime.alarms.heatRelayWearWarning ? "true" : "false") + ",";
  json += "\"coolRelayWearWarning\":" + String(runtime.alarms.coolRelayWearWarning ? "true" : "false") + ",";
  json += "\"compressorProtected\":" + String(runtime.alarms.compressorProtected ? "true" : "false") + ",";
  json += "\"startupInhibit\":" + String(runtime.alarms.outputStartupInhibit ? "true" : "false") + ",";
  json += "\"buzzerEnabled\":" + String(runtime.buzzerEnabled ? "true" : "false") + ",";
  json += "\"wifiApMode\":" + String(runtime.wifiApMode ? "true" : "false") + ",";
  json += "\"wifiSsid\":\"" + escapeJsonString(currentWiFiLabel()) + "\",";
  json += "\"wifiIp\":\"" + escapeJsonString(currentIpAddress()) + "\",";
  json += "\"wifiReconnectPending\":" + String(runtime.wifiReconnectRequested ? "true" : "false") + ",";
  json += "\"apClientCount\":" + String(runtime.wifiApMode ? WiFi.softAPgetStationNum() : 0) + ",";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"totalHeatOnMs\":" + String(runtime.totalHeatOnMs) + ",";
  json += "\"totalCoolOnMs\":" + String(runtime.totalCoolOnMs) + ",";
  json += "\"alarmCount\":" + String(runtime.alarmCount) + ",";
  json += "\"degradedCount\":" + String(runtime.degradedCount) + ",";
  json += "\"faultStopCount\":" + String(runtime.faultStopCount) + ",";
  json += "\"heatRelaySwitchCount\":" + String(runtime.heatRelaySwitchCount) + ",";
  json += "\"coolRelaySwitchCount\":" + String(runtime.coolRelaySwitchCount);
  json += "}";
  return json;
}

String buildHtmlPage(const String& actionMessage, bool isError) {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Fish Tank Controller</title>";
  html += "<style>body{font-family:Segoe UI,Arial,sans-serif;background:#eef6f7;color:#102a43;padding:20px;}";
  html += ".card{max-width:640px;margin:auto;background:#fff;border-radius:16px;padding:20px;box-shadow:0 10px 30px rgba(16,42,67,.12);}h1{margin-top:0;font-size:24px;}";
  html += ".grid{display:grid;grid-template-columns:repeat(2,minmax(120px,1fr));gap:12px;}";
  html += ".item{background:#f0f4f8;border-radius:12px;padding:12px;}small{display:block;color:#627d98;}strong{font-size:20px;}";
  html += ".notice{margin:14px 0;padding:12px;border-radius:12px;background:#fff3cd;color:#7c5700;}";
  html += ".notice-success{background:#d9fbe8;color:#166534;}";
  html += ".notice-error{background:#fde8e8;color:#991b1b;}ul{padding-left:20px;}li{margin:6px 0;}";
  html += "form{margin-top:18px;padding:16px;background:#f8fbfc;border-radius:12px;}label{display:block;margin:10px 0 6px;}input{width:100%;padding:10px;border:1px solid #bcccdc;border-radius:10px;box-sizing:border-box;}";
  html += ".checkbox-row{display:flex;align-items:center;gap:10px;margin:14px 0 6px;}.checkbox-row input{width:auto;padding:0;margin:0;}.checkbox-row label{display:flex;align-items:center;gap:10px;margin:0;font-weight:600;}";
  html += "button{margin-top:14px;padding:10px 16px;border:0;border-radius:10px;background:#0f766e;color:#fff;cursor:pointer;}button:hover{background:#115e59;}</style></head><body>";
  html += "<div class='card'><h1>x1nmu鱼缸温控器</h1>";
  html += "<div id='action-notice'>" + buildPageNotice(actionMessage, isError) + "</div>";
  html += "<div id='system-notice'>" + buildSystemNoticeHtml() + "</div>";
  html += "<div class='grid'>";
  html += buildStatusItem("status-state", "状态", stateToText(runtime.state));
  html += buildStatusItem("status-setpoint", "目标温度", formatFloat(runtime.setpoint) + " C");
  html += buildStatusItem("status-controlTemp", "控制温度", formatFloat(runtime.controlTemp) + " C");
  html += buildStatusItem("status-hysteresis", "回差", formatFloat(runtime.hysteresis) + " C");
  html += buildStatusItem("status-sensorDiff", "探头差值", formatFloat(runtime.sensorDiff) + " C");
  html += buildStatusItem("status-sensorA", "探头A", formatFloat(runtime.sensorA.temperature) + " C");
  html += buildStatusItem("status-sensorB", "探头B", formatFloat(runtime.sensorB.temperature) + " C");
  html += buildStatusItem("status-heatOn", "加热", String(runtime.heatOn ? "ON" : "OFF"));
  html += buildStatusItem("status-coolOn", "制冷", String(runtime.coolOn ? "ON" : "OFF"));
  html += buildStatusItem("status-lastFaultText", "最近故障", runtime.lastFaultText);
  html += buildStatusItem("status-faultDescription", "故障说明", faultCodeToDescription(runtime.lastFaultCode));
  html += buildStatusItem("status-faultLatched", "锁定保护", String(runtime.faultLatched ? "ON" : "OFF"));
  html += buildStatusItem("status-alarmCount", "告警次数", String(runtime.alarmCount));
  html += buildStatusItem("status-degradedCount", "降级次数", String(runtime.degradedCount));
  html += buildStatusItem("status-faultStopCount", "停机次数", String(runtime.faultStopCount));
  html += buildStatusItem("status-totalHeatOnMs", "累计加热", formatDurationMs(runtime.totalHeatOnMs));
  html += buildStatusItem("status-totalCoolOnMs", "累计制冷", formatDurationMs(runtime.totalCoolOnMs));
  html += buildStatusItem("status-heatRelaySwitchCount", "加热继电器动作", String(runtime.heatRelaySwitchCount));
  html += buildStatusItem("status-coolRelaySwitchCount", "制冷继电器动作", String(runtime.coolRelaySwitchCount));
  html += buildStatusItem("status-heatRelayWearWarning", "加热继电器预警", String(runtime.alarms.heatRelayWearWarning ? "ON" : "OFF"));
  html += buildStatusItem("status-coolRelayWearWarning", "制冷继电器预警", String(runtime.alarms.coolRelayWearWarning ? "ON" : "OFF"));
  html += buildStatusItem("status-lowTempCutoff", "低温切断", String(runtime.alarms.lowTempCutoff ? "ON" : "OFF"));
  html += buildStatusItem("status-highTempCutoff", "高温切断", String(runtime.alarms.highTempCutoff ? "ON" : "OFF"));
  html += buildStatusItem("status-startupInhibit", "启动保护", String(runtime.alarms.outputStartupInhibit ? "ON" : "OFF"));
  html += buildStatusItem("status-compressorProtected", "压缩机保护", String(runtime.alarms.compressorProtected ? "ON" : "OFF"));
  html += buildStatusItem("status-wifiApMode", "WiFi模式", String(runtime.wifiApMode ? "AP" : "STA"));
  html += buildStatusItem("status-wifiSsid", "WiFi名称", currentWiFiLabel());
  html += buildStatusItem("status-wifiIp", "WiFi地址", currentIpAddress());
  html += buildStatusItem("status-apClientCount", "AP客户端", String(runtime.wifiApMode ? WiFi.softAPgetStationNum() : 0));
  html += "</div><p id='status-wifi-summary'>WiFi: " + currentIpAddress() + "</p>";
  html += "<form id='control-settings-form' method='post' action='/settings/control'>";
  html += "<h2>控制参数</h2>";
  html += "<label for='setpoint'>目标温度 (20.0 - 32.0 C)</label>";
  html += "<input id='setpoint' name='setpoint' type='number' min='20' max='32' step='0.1' value='" + String(runtime.setpoint, 1) + "'>";
  html += "<label for='hysteresis'>回差 (0.3 - 2.0 C)</label>";
  html += "<input id='hysteresis' name='hysteresis' type='number' min='0.3' max='2.0' step='0.1' value='" + String(runtime.hysteresis, 1) + "'>";
  html += "<label for='sensorDiffAlarm'>探头差值告警阈值 (0.3 - 3.0 C)</label>";
  html += "<input id='sensorDiffAlarm' name='sensorDiffAlarm' type='number' min='0.3' max='3.0' step='0.1' value='" + String(runtime.sensorDiffAlarm, 1) + "'>";
  html += "<div class='checkbox-row'><label for='buzzerEnabled'><input id='buzzerEnabled' name='buzzerEnabled' type='checkbox' value='1'" + String(runtime.buzzerEnabled ? " checked" : "") + "> 启用蜂鸣器</label></div>";
  html += "<button type='submit'>保存控制参数</button>";
  html += "</form>";
  html += "<form id='wifi-settings-form' method='post' action='/settings/wifi'>";
  html += "<h2>WiFi 设置</h2>";
  html += "<label for='wifiSsid'>WiFi SSID</label>";
  html += "<input id='wifiSsid' name='wifiSsid' type='text' maxlength='32' value='" + String(runtime.wifiSsid) + "'>";
  html += "<label for='wifiPassword'>WiFi 密码</label>";
  html += "<input id='wifiPassword' name='wifiPassword' type='password' maxlength='64' placeholder='留空表示不修改密码' value=''>";
  html += "<button type='submit'>保存 WiFi 并重连</button>";
  html += "</form>";
  html += "<form id='fault-reset-form' method='post' action='/faults/reset'>";
  html += "<h2>故障复位</h2>";
  html += "<p>仅在确认探头、执行器和水温正常后使用。也可本地长按 SET+UP 执行。</p>";
  html += "<button type='submit'>清除锁定故障</button>";
  html += "</form>";
  html += "<form id='stats-reset-form' method='post' action='/stats/reset'>";
  html += "<h2>统计清零</h2>";
  html += "<p>清零累计加热、累计制冷、继电器动作次数和状态计数。也可本地长按 SET+DOWN 执行。</p>";
  html += "<button type='submit'>清零统计</button>";
  html += "</form></div>";
  html += "<script>const statusFormatters={state:v=>v!=null?v:'--',setpoint:v=>Number.isFinite(v)?v.toFixed(1)+' C':'--',controlTemp:v=>Number.isFinite(v)?v.toFixed(2)+' C':'--',hysteresis:v=>Number.isFinite(v)?v.toFixed(1)+' C':'--',sensorDiffAlarm:v=>Number.isFinite(v)?v.toFixed(1):'--',sensorA:v=>Number.isFinite(v)?v.toFixed(2)+' C':'--',sensorB:v=>Number.isFinite(v)?v.toFixed(2)+' C':'--',sensorDiff:(v,s)=>Number.isFinite(s.sensorA)&&Number.isFinite(s.sensorB)?Math.abs(s.sensorA-s.sensorB).toFixed(2)+' C':'--',heatOn:v=>v?'ON':'OFF',coolOn:v=>v?'ON':'OFF',lastFaultText:v=>v!=null?v:'--',faultDescription:v=>v!=null?v:'--',faultLatched:v=>v?'ON':'OFF',alarmCount:v=>String(v!=null?v:0),degradedCount:v=>String(v!=null?v:0),faultStopCount:v=>String(v!=null?v:0),totalHeatOnMs:v=>formatDuration(v),totalCoolOnMs:v=>formatDuration(v),heatRelaySwitchCount:v=>String(v!=null?v:0),coolRelaySwitchCount:v=>String(v!=null?v:0),heatRelayWearWarning:v=>v?'ON':'OFF',coolRelayWearWarning:v=>v?'ON':'OFF',lowTempCutoff:v=>v?'ON':'OFF',highTempCutoff:v=>v?'ON':'OFF',startupInhibit:v=>v?'ON':'OFF',compressorProtected:v=>v?'ON':'OFF',wifiApMode:v=>v?'AP':'STA',wifiSsid:v=>v!=null?v:'--',wifiIp:v=>v!=null?v:'--',apClientCount:v=>String(v!=null?v:0)};";
  html += "function formatDuration(ms){if(!Number.isFinite(ms)){return '--';}const totalSeconds=Math.floor(ms/1000);const hours=Math.floor(totalSeconds/3600);const minutes=Math.floor((totalSeconds%3600)/60);const seconds=totalSeconds%60;return hours+':' + String(minutes).padStart(2,'0') + ':' + String(seconds).padStart(2,'0');}";
  html += "function escapeHtml(text){return String(text!=null?text:'').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('\"','&quot;').replaceAll(\"'\",'&#39;');}";
  html += "function renderNotice(message,isError){const host=document.getElementById('action-notice');if(!host){return;}if(!message){host.innerHTML='';return;}host.innerHTML='<div class=\"notice '+(isError?'notice-error':'notice-success')+'\">'+escapeHtml(message)+'</div>';window.scrollTo({top:0,behavior:'smooth'});}";
  html += "function renderSystemNotice(status){const host=document.getElementById('system-notice');if(!host){return;}if(status.wifiApMode){host.innerHTML='<div class=\"notice\">当前处于 AP 配网模式。请连接热点 <strong>'+escapeHtml(status.wifiSsid!=null?status.wifiSsid:'--')+'</strong>，密码 <strong>" + String(ControlConfig::AP_PASSWORD) + "</strong>，然后在本页填写路由器账号。</div>';return;}if(status.wifiReconnectPending){host.innerHTML='<div class=\"notice\">WiFi 参数已保存，系统正在尝试重新连接路由器。</div>';return;}if(status.faultLatched){host.innerHTML='<div class=\"notice\">系统处于硬保护锁定状态，请先检查温度和执行机构，再手动复位。</div>';return;}if(status.heatRelayWearWarning||status.coolRelayWearWarning){host.innerHTML='<div class=\"notice\">继电器动作次数已接近维护阈值，建议检查触点寿命并预备更换。</div>';return;}const sta=status.wifiConnected&&status.wifiIp?('http://'+status.wifiIp):'STA 未连接';host.innerHTML='<div class=\"notice\">访问入口：'+escapeHtml(sta)+'；AP 配网 http://192.168.4.1</div>';};";
  html += "function updateStatusField(name,value,status){const element=document.getElementById('status-'+name);if(!element){return;}const formatter=statusFormatters[name];element.textContent=formatter?formatter(value,status):String(value!=null?value:'--');}";
  html += "async function refreshStatus(){try{const response=await fetch('/status',{cache:'no-store'});if(!response.ok){return;}const status=await response.json();Object.keys(statusFormatters).forEach(key=>updateStatusField(key,status[key],status));const summary=document.getElementById('status-wifi-summary');if(summary){summary.textContent='WiFi: '+(status.wifiIp!=null?status.wifiIp:'--');}renderSystemNotice(status);}catch(error){console.log('status refresh failed',error);}}";
  html += "async function handleAjaxFormSubmit(event){event.preventDefault();const form=event.currentTarget;const data=new URLSearchParams(new FormData(form));const action=form.getAttribute('action')+(form.getAttribute('action').includes('?')?'&':'?')+'ajax=1';try{const response=await fetch(action,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:data.toString(),cache:'no-store'});const result=await response.json();renderNotice(result.message,!response.ok||result.ok===false);if(response.ok&&result.ok!==false){await refreshStatus();if(form.id==='wifi-settings-form'){let retry=0;const timer=setInterval(async()=>{retry++;try{const statusResp=await fetch('/status',{cache:'no-store'});if(statusResp.ok){const status=await statusResp.json();if(status.wifiConnected===true&&status.wifiReconnectPending===false){clearInterval(timer);location.reload();return;}}}catch(_err){}if(retry>=12){clearInterval(timer);renderNotice('重连等待超时，请手动刷新页面查看当前状态。',true);}},2000);}}}catch(error){renderNotice('请求失败，请稍后重试。',true);console.log('form submit failed',error);}}";
  html += "['control-settings-form','wifi-settings-form','fault-reset-form','stats-reset-form'].forEach(id=>{const form=document.getElementById(id);if(form){form.addEventListener('submit',handleAjaxFormSubmit);}});";
  html += "refreshStatus();";
  html += "setInterval(()=>{refreshStatus();},5000);</script>";
  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", buildHtmlPage());
}

void handleStatus() {
  server.send(200, "application/json", buildJsonStatus());
}

void handleSetpoint() {
  if (!server.hasArg("value")) {
    sendActionResponse(400, "缺少 value 参数，未修改目标温度。", true);
    return;
  }

  float newValue = server.arg("value").toFloat();
  if (newValue < ControlConfig::MIN_SETPOINT || newValue > ControlConfig::MAX_SETPOINT) {
    sendActionResponse(400, "目标温度超出允许范围，必须在 20.0C 到 32.0C 之间。", true);
    return;
  }

  runtime.setpoint = newValue;
  markSettingsDirty();
  pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  sendActionResponse(200, String("目标温度已更新为 ") + formatFloat(runtime.setpoint) + " C。", false);
}

void handleControlSettings() {
  bool hasSetpoint = server.hasArg("setpoint");
  bool hasHysteresis = server.hasArg("hysteresis");
  bool hasSensorDiffAlarm = server.hasArg("sensorDiffAlarm");
  bool hasBuzzerEnabled = server.hasArg("buzzerEnabled");

  if (!hasSetpoint && !hasHysteresis && !hasSensorDiffAlarm && !hasBuzzerEnabled) {
    sendActionResponse(400, "没有收到任何设置项，未执行保存。", true);
    return;
  }

  if (hasSetpoint) {
    float newSetpoint = server.arg("setpoint").toFloat();
    if (newSetpoint < ControlConfig::MIN_SETPOINT || newSetpoint > ControlConfig::MAX_SETPOINT) {
      sendActionResponse(400, "目标温度超出允许范围，必须在 20.0C 到 32.0C 之间。", true);
      return;
    }
    runtime.setpoint = newSetpoint;
  }

  if (hasHysteresis) {
    float newHysteresis = server.arg("hysteresis").toFloat();
    if (newHysteresis < ControlConfig::MIN_HYSTERESIS || newHysteresis > ControlConfig::MAX_HYSTERESIS) {
      sendActionResponse(400, "回差超出允许范围，必须在 0.3C 到 2.0C 之间。", true);
      return;
    }
    runtime.hysteresis = newHysteresis;
  }

  if (hasSensorDiffAlarm) {
    float newSensorDiffAlarm = server.arg("sensorDiffAlarm").toFloat();
    if (newSensorDiffAlarm < ControlConfig::MIN_SENSOR_DIFF_ALARM || newSensorDiffAlarm > ControlConfig::MAX_SENSOR_DIFF_ALARM) {
      sendActionResponse(400, "探头差值告警阈值超出允许范围，必须在 0.3C 到 3.0C 之间。", true);
      return;
    }
    runtime.sensorDiffAlarm = newSensorDiffAlarm;
  }

  runtime.buzzerEnabled = hasBuzzerEnabled;

  markSettingsDirty();
  pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  logEvent(String("Settings updated from web: setpoint=") + formatFloat(runtime.setpoint) +
           " hysteresis=" + formatFloat(runtime.hysteresis) +
           " diffAlarm=" + formatFloat(runtime.sensorDiffAlarm) +
           " buzzer=" + String(runtime.buzzerEnabled ? "ON" : "OFF"));
  sendActionResponse(200, "控制参数已保存，页面数据已刷新。", false);
}

void handleWiFiSettings() {
  bool hasWiFiSsid = server.hasArg("wifiSsid");
  bool hasWiFiPassword = server.hasArg("wifiPassword");

  if (!hasWiFiSsid && !hasWiFiPassword) {
    sendActionResponse(400, "未收到 WiFi 参数，未执行保存。", true);
    return;
  }

  if (hasWiFiSsid) {
    copyStringToBuffer(server.arg("wifiSsid"), runtime.wifiSsid, sizeof(runtime.wifiSsid));
  }
  if (hasWiFiPassword) {
    String newPassword = server.arg("wifiPassword");
    newPassword.trim();
    if (newPassword.length() > 0) {
      copyStringToBuffer(newPassword, runtime.wifiPassword, sizeof(runtime.wifiPassword));
    }
  }

  if (runtime.wifiSsid[0] == '\0') {
    sendActionResponse(400, "WiFi SSID 不能为空。", true);
    return;
  }

  markSettingsDirty();
  runtime.wifiReconnectRequested = true;
  runtime.wifiReconnectDueMs = millis() + ControlConfig::WIFI_SAVE_RECONNECT_DELAY_MS;
  pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  logEvent(String("WiFi settings updated from web: wifiSsid=") + String(runtime.wifiSsid));
  sendActionResponse(200, "WiFi 设置已保存，系统正在重连并会自动刷新页面。", false);
}

void handleFaultReset() {
  clearFaultLatch();
  pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  sendActionResponse(200, "锁定故障已清除。", false);
}

void handleStatsReset() {
  clearRuntimeStatistics();
  pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  sendActionResponse(200, "运行统计已清零。", false);
}

void startConfigAP() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  String chipId = String((uint32_t)ESP.getEfuseMac(), HEX);
  chipId.toUpperCase();
  String apSsid = String(ControlConfig::AP_SSID_PREFIX) + "-" + chipId.substring(chipId.length() > 4 ? chipId.length() - 4 : 0);
  copyStringToBuffer(apSsid, runtime.wifiApSsid, sizeof(runtime.wifiApSsid));
  WiFi.softAP(runtime.wifiApSsid, ControlConfig::AP_PASSWORD);
  runtime.wifiApMode = true;
  runtime.wifiConnectStartedMs = 0;
  runtime.wifiApStartedMs = millis();
  logEvent(String("WiFi AP started: SSID=") + runtime.wifiApSsid + " IP=" + WiFi.softAPIP().toString());
}

}  // namespace

String currentWiFiLabel() {
  if (runtime.wifiApMode) {
    return String(runtime.wifiApSsid);
  }
  if (WiFi.status() == WL_CONNECTED) {
    return String(WiFi.SSID());
  }
  if (hasConfiguredWiFi()) {
    return String(runtime.wifiSsid);
  }
  return "--";
}

String currentIpAddress() {
  if (runtime.wifiApMode) {
    return WiFi.softAPIP().toString();
  }
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return "--";
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/set", HTTP_POST, handleSetpoint);
  server.on("/settings", HTTP_POST, handleControlSettings);
  server.on("/settings/control", HTTP_POST, handleControlSettings);
  server.on("/settings/wifi", HTTP_POST, handleWiFiSettings);
  server.on("/faults/reset", HTTP_POST, handleFaultReset);
  server.on("/stats/reset", HTTP_POST, handleStatsReset);
  server.begin();
  logEvent("Web server started");
}

void connectWiFi(bool forceReconnect) {
  if (!forceReconnect && WiFi.status() == WL_CONNECTED && !runtime.wifiApMode) {
    return;
  }

  if (!hasConfiguredWiFi()) {
    logEvent("No STA credential, entering AP config mode");
    startConfigAP();
    return;
  }

  runtime.wifiReconnectRequested = false;
  runtime.wifiReconnectDueMs = 0;
  runtime.lastWiFiRetryMs = millis();
  runtime.wifiConnectStartedMs = runtime.lastWiFiRetryMs;
  runtime.wifiApMode = false;
  runtime.wifiApSsid[0] = '\0';

  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);

  const char* ssid = runtime.wifiSsid;
  const char* password = runtime.wifiPassword;

  logEvent(String("WiFi connecting to ") + ssid + "...");
  WiFi.begin(ssid, password);
}

void maintainWiFi() {
  if (runtime.wifiApMode) {
    bool shouldRetrySta = hasConfiguredWiFi() &&
                          WiFi.softAPgetStationNum() == 0 &&
                          millis() - runtime.wifiApStartedMs >= ControlConfig::WIFI_AP_RETRY_INTERVAL_MS;
    if (shouldRetrySta) {
      logEvent("AP idle timeout reached, retrying STA connection");
      connectWiFi(true);
    }
    return;
  }

  if (runtime.wifiReconnectRequested && runtime.wifiReconnectDueMs != 0 && millis() >= runtime.wifiReconnectDueMs) {
    connectWiFi(true);
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (runtime.wifiConnectStartedMs != 0) {
      runtime.wifiConnectStartedMs = 0;
      logEvent(String("WiFi connected, SSID=") + WiFi.SSID() + " IP=" + WiFi.localIP().toString());
    }
    return;
  }

  if (runtime.wifiConnectStartedMs != 0 && millis() - runtime.wifiConnectStartedMs >= ControlConfig::WIFI_CONNECT_TIMEOUT_MS) {
    runtime.wifiConnectStartedMs = 0;
    logEvent("WiFi connection timeout, switching to AP config mode");
    startConfigAP();
    return;
  }

  if (runtime.wifiConnectStartedMs == 0 && millis() - runtime.lastWiFiRetryMs >= ControlConfig::WIFI_RETRY_INTERVAL_MS) {
    connectWiFi(true);
  }
}