#include <math.h>
#include <cstring>

#include "app_runtime.h"
#include "app_network.h"

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

void logEvent(const String& message) {
  runtime.eventLog[runtime.eventLogHead] = String("[") + millis() + " ms] " + message;
  runtime.eventLogHead = (runtime.eventLogHead + 1) % ControlConfig::EVENT_LOG_SIZE;
  if (runtime.eventLogCount < ControlConfig::EVENT_LOG_SIZE) {
    runtime.eventLogCount++;
  }

  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] ");
  Serial.println(message);
}

void logPeriodicStatus() {
  uint32_t conversionAgeMs = 0;

  if (millis() - runtime.lastDebugMs < ControlConfig::DEBUG_PRINT_INTERVAL_MS) {
    return;
  }
  runtime.lastDebugMs = millis();
  if (runtime.sensorConversionPending) {
    conversionAgeMs = millis() - runtime.sensorRequestMs;
  }

  Serial.print("STATE=");
  Serial.print(stateToText(runtime.state));
  Serial.print(" T1=");
  Serial.print(formatFloat(runtime.sensorA.temperature));
  Serial.print(" T2=");
  Serial.print(formatFloat(runtime.sensorB.temperature));
  Serial.print(" TC=");
  Serial.print(formatFloat(runtime.controlTemp));
  Serial.print(" SET=");
  Serial.print(formatFloat(runtime.setpoint));
  Serial.print(" HYS=");
  Serial.print(formatFloat(runtime.hysteresis));
  Serial.print(" HEAT=");
  Serial.print(runtime.heatOn ? "ON" : "OFF");
  Serial.print(" COOL=");
  Serial.print(runtime.coolOn ? "ON" : "OFF");
  Serial.print(" WIFI=");
  if (runtime.wifiApMode) {
    Serial.print("AP");
  } else {
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "DISC");
  }
  Serial.print(" IP=");
  Serial.print(currentIpAddress());
  Serial.print(" SAMPLE=");
  Serial.print(runtime.lastSampleCycleMs);
  Serial.print("ms MAX=");
  Serial.print(runtime.maxSampleCycleMs);
  Serial.print("ms CONV_AGE=");
  Serial.print(conversionAgeMs);
  Serial.println("ms");
}

void markSettingsDirty() {
  runtime.settingsDirty = true;
  runtime.lastSettingsChangedMs = millis();
}

void markRelayStatsDirty() {
  runtime.relayStatsDirty = true;
  runtime.lastRelayStatsChangedMs = millis();
}

void updateRelayWearWarnings() {
  runtime.alarms.heatRelayWearWarning = runtime.heatRelaySwitchCount >= ControlConfig::RELAY_WARN_SWITCH_COUNT;
  runtime.alarms.coolRelayWearWarning = runtime.coolRelaySwitchCount >= ControlConfig::RELAY_WARN_SWITCH_COUNT;
}

void loadSettings() {
  preferences.begin("fish-tank", false);
  runtime.setpoint = ControlConfig::DEFAULT_SETPOINT;
  if (preferences.isKey("setpoint")) {
    runtime.setpoint = preferences.getFloat("setpoint", ControlConfig::DEFAULT_SETPOINT);
  }
  runtime.setpoint = clampFloat(runtime.setpoint, ControlConfig::MIN_SETPOINT, ControlConfig::MAX_SETPOINT);

  runtime.hysteresis = ControlConfig::DEFAULT_HYSTERESIS;
  if (preferences.isKey("hysteresis")) {
    runtime.hysteresis = preferences.getFloat("hysteresis", ControlConfig::DEFAULT_HYSTERESIS);
  }
  runtime.hysteresis = clampFloat(runtime.hysteresis, ControlConfig::MIN_HYSTERESIS, ControlConfig::MAX_HYSTERESIS);

  runtime.sensorDiffAlarm = ControlConfig::DEFAULT_SENSOR_DIFF_ALARM;
  if (preferences.isKey("sd_alarm")) {
    runtime.sensorDiffAlarm = preferences.getFloat("sd_alarm", ControlConfig::DEFAULT_SENSOR_DIFF_ALARM);
  }
  runtime.sensorDiffAlarm = clampFloat(
      runtime.sensorDiffAlarm,
      ControlConfig::MIN_SENSOR_DIFF_ALARM,
      ControlConfig::MAX_SENSOR_DIFF_ALARM);

  runtime.buzzerEnabled = ControlConfig::DEFAULT_BUZZER_ENABLED;
  if (preferences.isKey("buzzer_en")) {
    runtime.buzzerEnabled = preferences.getBool("buzzer_en", ControlConfig::DEFAULT_BUZZER_ENABLED);
  }

  runtime.heatRelaySwitchCount = preferences.isKey("heat_sw") ? preferences.getUInt("heat_sw", 0) : 0;
  runtime.coolRelaySwitchCount = preferences.isKey("cool_sw") ? preferences.getUInt("cool_sw", 0) : 0;

  runtime.wifiSsid[0] = '\0';
  runtime.wifiPassword[0] = '\0';
  if (preferences.isKey("wifi_ssid")) {
    preferences.getString("wifi_ssid", runtime.wifiSsid, sizeof(runtime.wifiSsid));
  }
  if (preferences.isKey("wifi_pwd")) {
    preferences.getString("wifi_pwd", runtime.wifiPassword, sizeof(runtime.wifiPassword));
  }
  if (runtime.wifiSsid[0] == '\0') {
    strncpy(runtime.wifiSsid, ControlConfig::DEFAULT_WIFI_SSID, sizeof(runtime.wifiSsid) - 1);
  }
  if (runtime.wifiPassword[0] == '\0') {
    strncpy(runtime.wifiPassword, ControlConfig::DEFAULT_WIFI_PASSWORD, sizeof(runtime.wifiPassword) - 1);
  }
  updateRelayWearWarnings();
  logEvent(String("Settings loaded: setpoint=") + formatFloat(runtime.setpoint) +
           " hysteresis=" + formatFloat(runtime.hysteresis) +
           " diffAlarm=" + formatFloat(runtime.sensorDiffAlarm) +
           " buzzer=" + String(runtime.buzzerEnabled ? "ON" : "OFF") +
           " wifiSsid=" + String(runtime.wifiSsid) +
           " heatSw=" + String(runtime.heatRelaySwitchCount) +
           " coolSw=" + String(runtime.coolRelaySwitchCount));
}

void saveSettingsIfNeeded() {
  bool shouldSaveSettings = runtime.settingsDirty &&
                            millis() - runtime.lastSettingsChangedMs >= ControlConfig::SETTINGS_SAVE_DELAY_MS;
  bool shouldSaveRelayStats = runtime.relayStatsDirty &&
                              millis() - runtime.lastRelayStatsChangedMs >= ControlConfig::RELAY_STATS_SAVE_DELAY_MS;

  if (!shouldSaveSettings && !shouldSaveRelayStats) {
    return;
  }

  if (shouldSaveSettings) {
    preferences.putFloat("setpoint", runtime.setpoint);
    preferences.putFloat("hysteresis", runtime.hysteresis);
    preferences.putFloat("sd_alarm", runtime.sensorDiffAlarm);
    preferences.putBool("buzzer_en", runtime.buzzerEnabled);
    preferences.putString("wifi_ssid", runtime.wifiSsid);
    preferences.putString("wifi_pwd", runtime.wifiPassword);
    runtime.settingsDirty = false;
    logEvent(String("Settings saved: setpoint=") + formatFloat(runtime.setpoint) +
             " hysteresis=" + formatFloat(runtime.hysteresis) +
             " diffAlarm=" + formatFloat(runtime.sensorDiffAlarm) +
             " buzzer=" + String(runtime.buzzerEnabled ? "ON" : "OFF") +
             " wifiSsid=" + String(runtime.wifiSsid));
  }

  if (shouldSaveRelayStats) {
    preferences.putUInt("heat_sw", runtime.heatRelaySwitchCount);
    preferences.putUInt("cool_sw", runtime.coolRelaySwitchCount);
    runtime.relayStatsDirty = false;
    logEvent(String("Relay stats saved: heatSw=") + String(runtime.heatRelaySwitchCount) +
             " coolSw=" + String(runtime.coolRelaySwitchCount));
  }
}

String stateToText(SystemState state) {
  switch (state) {
    case SystemState::Init:
      return "INIT";
    case SystemState::Idle:
      return "IDLE";
    case SystemState::Heating:
      return "HEATING";
    case SystemState::Cooling:
      return "COOLING";
    case SystemState::Degraded:
      return "DEGRADED";
    case SystemState::Alarm:
      return "ALARM";
    case SystemState::FaultStop:
      return "FAULT_STOP";
  }
  return "UNKNOWN";
}

String faultCodeToText(FaultCode code) {
  switch (code) {
    case FaultCode::None:
      return "NONE";
    case FaultCode::SensorAFailed:
      return "SNS_A";
    case FaultCode::SensorBFailed:
      return "SNS_B";
    case FaultCode::BothSensorsFailed:
      return "SNS_AB";
    case FaultCode::SensorMismatch:
      return "MISMATCH";
    case FaultCode::WiFiDisconnected:
      return "WIFI";
    case FaultCode::ControlTempInvalid:
      return "TEMP_INV";
    case FaultCode::LowTempCutoff:
      return "LOW_CUT";
    case FaultCode::HighTempCutoff:
      return "HIGH_CUT";
  }
  return "UNKNOWN";
}

String faultCodeToDescription(FaultCode code) {
  switch (code) {
    case FaultCode::None:
      return "无故障";
    case FaultCode::SensorAFailed:
      return "探头A失效";
    case FaultCode::SensorBFailed:
      return "探头B失效";
    case FaultCode::BothSensorsFailed:
      return "双探头均失效";
    case FaultCode::SensorMismatch:
      return "双探头温差超限";
    case FaultCode::WiFiDisconnected:
      return "WiFi连接中断";
    case FaultCode::ControlTempInvalid:
      return "控制温度无效";
    case FaultCode::LowTempCutoff:
      return "控制温度低于安全下限";
    case FaultCode::HighTempCutoff:
      return "控制温度高于安全上限";
  }
  return "未知故障";
}

String formatFloat(float value, uint8_t digits) {
  if (isnan(value)) {
    return "--";
  }
  return String(value, static_cast<unsigned int>(digits));
}

String menuItemLabel(MenuItem item) {
  switch (item) {
    case MenuItem::Setpoint:
      return "SETPOINT";
    case MenuItem::Hysteresis:
      return "HYST";
    case MenuItem::SensorDiffAlarm:
      return "DIFF ALM";
    case MenuItem::Buzzer:
      return "BUZZER";
    case MenuItem::Count:
      break;
  }
  return "UNKNOWN";
}

String menuItemValue(MenuItem item) {
  switch (item) {
    case MenuItem::Setpoint:
      return formatFloat(runtime.setpoint);
    case MenuItem::Hysteresis:
      return formatFloat(runtime.hysteresis);
    case MenuItem::SensorDiffAlarm:
      return formatFloat(runtime.sensorDiffAlarm);
    case MenuItem::Buzzer:
      return runtime.buzzerEnabled ? "ON" : "OFF";
    case MenuItem::Count:
      break;
  }
  return "--";
}

namespace {

void setHeatOutput(bool enabled);
void setCoolOutput(bool enabled);

String oledMaintenanceHint() {
  if (runtime.faultLatched) {
    return "SET+UP CLR FAULT";
  }
  if (runtime.alarms.heatRelayWearWarning || runtime.alarms.coolRelayWearWarning) {
    return "SET+DN CLR STATS";
  }
  return "SET SW PAGE";
}

String oledStatsSummary() {
  return String("H") + runtime.heatRelaySwitchCount + " C" + runtime.coolRelaySwitchCount;
}

bool relayLevelFor(bool enabled) {
  return enabled ? ControlConfig::RELAY_ACTIVE_LEVEL : !ControlConfig::RELAY_ACTIVE_LEVEL;
}

bool outputsStartupInhibited() {
  bool inhibited = millis() - runtime.bootMs < ControlConfig::OUTPUT_STARTUP_INHIBIT_MS;
  runtime.alarms.outputStartupInhibit = inhibited;
  return inhibited;
}

void updateSafetyCutoffs() {
  runtime.alarms.lowTempCutoff = false;
  runtime.alarms.highTempCutoff = false;

  if (isnan(runtime.controlTemp)) {
    return;
  }

  runtime.alarms.lowTempCutoff = runtime.controlTemp <= ControlConfig::ABS_LOW_TEMP_CUTOFF;
  runtime.alarms.highTempCutoff = runtime.controlTemp >= ControlConfig::ABS_HIGH_TEMP_CUTOFF;
}

void setLastFault(FaultCode code) {
  runtime.lastFaultCode = code;
  runtime.lastFaultText = faultCodeToText(code);
}

void updateRuntimeTotals() {
  uint32_t now = millis();
  if (runtime.lastStatsUpdateMs == 0) {
    runtime.lastStatsUpdateMs = now;
    return;
  }

  uint32_t delta = now - runtime.lastStatsUpdateMs;
  runtime.lastStatsUpdateMs = now;

  if (runtime.heatOn) {
    runtime.totalHeatOnMs += delta;
  }
  if (runtime.coolOn) {
    runtime.totalCoolOnMs += delta;
  }
}

void setHeatOutput(bool enabled) {
  if (enabled && runtime.coolOn) {
    setCoolOutput(false);
  }
  if (runtime.heatOn == enabled) {
    return;
  }
  runtime.heatOn = enabled;
  runtime.heatRelaySwitchCount++;
  markRelayStatsDirty();
  updateRelayWearWarnings();
  digitalWrite(PinMap::HEAT_CTRL, relayLevelFor(enabled));
  runtime.heatStateChangedMs = millis();
  logEvent(String("Heat output -> ") + (enabled ? "ON" : "OFF"));
}

void setCoolOutput(bool enabled) {
  if (enabled && runtime.heatOn) {
    setHeatOutput(false);
  }
  if (runtime.coolOn == enabled) {
    return;
  }
  runtime.coolOn = enabled;
  runtime.coolRelaySwitchCount++;
  markRelayStatsDirty();
  updateRelayWearWarnings();
  digitalWrite(PinMap::COOL_CTRL, relayLevelFor(enabled));
  runtime.coolStateChangedMs = millis();
  logEvent(String("Cool output -> ") + (enabled ? "ON" : "OFF"));
}

void stopAllOutputs() {
  setHeatOutput(false);
  setCoolOutput(false);
}

void enterLatchedFault(FaultCode code, SystemState previousState, const String& reason) {
  stopAllOutputs();
  runtime.faultLatched = true;
  runtime.alarms.faultLatched = true;
  runtime.state = SystemState::FaultStop;
  setLastFault(code);

  if (previousState != SystemState::FaultStop) {
    runtime.faultStopCount++;
    pulseBuzzer(ControlConfig::BUZZ_PULSE_MS * 3);
  }
  logEvent(reason);
}

bool readKeyPressed(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

bool isTemperaturePlausible(float temp) {
  return temp >= ControlConfig::MIN_VALID_TEMP && temp <= ControlConfig::MAX_VALID_TEMP;
}

bool isLargeTemperatureJump(const SensorChannel& channel, float value) {
  return !isnan(channel.lastRawTemperature) && fabs(value - channel.lastRawTemperature) > ControlConfig::MAX_SENSOR_STEP_DELTA;
}

void acceptSensorSample(SensorChannel& channel, float value) {
  channel.lastRawTemperature = value;
  if (isnan(channel.temperature)) {
    channel.temperature = value;
  } else {
    channel.temperature += (value - channel.temperature) * ControlConfig::SENSOR_FILTER_ALPHA;
  }

  channel.failCount = 0;
  if (channel.recoverCount < 255) {
    channel.recoverCount++;
  }
  if (channel.recoverCount >= ControlConfig::SENSOR_RECOVER_VALID_COUNT) {
    channel.valid = true;
  }
}

void rejectSensorSample(SensorChannel& channel) {
  channel.recoverCount = 0;
  if (channel.failCount < 255) {
    channel.failCount++;
  }
  if (channel.failCount >= ControlConfig::SENSOR_FAIL_LIMIT) {
    channel.valid = false;
    channel.temperature = NAN;
  }
}

bool coolingProtected() {
  if (runtime.coolOn) {
    runtime.alarms.compressorProtected = false;
    return false;
  }
  uint32_t elapsedOff = millis() - runtime.coolStateChangedMs;
  bool protectedState = elapsedOff < ControlConfig::COOL_MIN_OFF_MS;
  runtime.alarms.compressorProtected = protectedState;
  return protectedState;
}

void drawMenuPage() {
  MenuItem item = static_cast<MenuItem>(runtime.menuIndex);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("LOCAL MENU");

  display.setCursor(0, 14);
  display.print(menuItemLabel(item));

  display.setTextSize(2);
  display.setCursor(0, 28);
  display.print(menuItemValue(item));

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("SET next/hold exit");
}

void drawDisplayPageOne() {
  String modeText = stateToText(runtime.state);
  if (modeText.length() > 4) {
    modeText = modeText.substring(0, 4);
  }

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("TC:");
  display.print(formatFloat(runtime.controlTemp));
  display.print("C SET:");
  display.print(formatFloat(runtime.setpoint));

  display.setCursor(0, 12);
  display.print("MODE:");
  display.print(modeText);
  display.setCursor(78, 12);
  display.print("H");
  display.print(runtime.heatOn ? "ON" : "OF");
  display.print(" C");
  display.print(runtime.coolOn ? "ON" : "OF");

  display.setCursor(0, 24);
  display.print("T1:");
  display.print(formatFloat(runtime.sensorA.temperature));
  display.setCursor(64, 24);
  display.print("T2:");
  display.print(formatFloat(runtime.sensorB.temperature));

  display.setCursor(0, 36);
  display.print("FLT:");
  display.print(runtime.lastFaultText);

  display.setCursor(0, 48);
  display.print("IP:");
  display.print(currentIpAddress());

  display.setCursor(0, 56);
  if (runtime.faultLatched) {
    display.print("HOLD SET UP CLRFLT");
  } else {
    display.print("UPDN SET  HOLD FAST");
  }
}

void drawDisplayPageTwo() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("HEAT:");
  display.print(runtime.heatOn ? "ON" : "OFF");
  display.print(" COOL:");
  display.print(runtime.coolOn ? "ON" : "OFF");

  display.setCursor(0, 12);
  display.print("FLT:");
  display.print(runtime.lastFaultText);

  display.setCursor(0, 24);
  display.print(oledStatsSummary());

  display.setCursor(0, 36);
  display.print("ALM:");
  display.print(runtime.alarmCount);
  display.print(" DEG:");
  display.print(runtime.degradedCount);

  display.setCursor(0, 48);
  display.print(oledMaintenanceHint());
}

}  // namespace

void setBuzzer(bool enabled) {
  digitalWrite(PinMap::BUZZ_CTRL, enabled ? ControlConfig::BUZZER_ACTIVE_LEVEL : !ControlConfig::BUZZER_ACTIVE_LEVEL);
}

void pulseBuzzer(uint32_t durationMs) {
  if (!runtime.buzzerEnabled) {
    return;
  }
  setBuzzer(true);
  runtime.buzzerOffMs = millis() + durationMs;
}

void updateSensors() {
  if (!runtime.sensorConversionPending) {
    sensorBusA.requestTemperatures();
    sensorBusB.requestTemperatures();
    runtime.sensorRequestMs = millis();
    runtime.sensorConversionPending = true;
    return;
  }

  if (millis() - runtime.sensorRequestMs < ControlConfig::SENSOR_CONVERSION_MS) {
    return;
  }

  float valueA = sensorBusA.getTempCByIndex(0);
  bool validA = valueA != DEVICE_DISCONNECTED_C && isTemperaturePlausible(valueA);
  if (validA && isLargeTemperatureJump(runtime.sensorA, valueA)) {
    validA = false;
  }
  if (validA) {
    acceptSensorSample(runtime.sensorA, valueA);
  } else {
    rejectSensorSample(runtime.sensorA);
  }

  float valueB = sensorBusB.getTempCByIndex(0);
  bool validB = valueB != DEVICE_DISCONNECTED_C && isTemperaturePlausible(valueB);
  if (validB && isLargeTemperatureJump(runtime.sensorB, valueB)) {
    validB = false;
  }
  if (validB) {
    acceptSensorSample(runtime.sensorB, valueB);
  } else {
    rejectSensorSample(runtime.sensorB);
  }

  runtime.alarms.sensorAFailed = !runtime.sensorA.valid && runtime.sensorA.failCount >= ControlConfig::SENSOR_FAIL_LIMIT;
  runtime.alarms.sensorBFailed = !runtime.sensorB.valid && runtime.sensorB.failCount >= ControlConfig::SENSOR_FAIL_LIMIT;
  runtime.alarms.bothSensorsFailed = runtime.alarms.sensorAFailed && runtime.alarms.sensorBFailed;

  if (runtime.sensorA.valid && runtime.sensorB.valid) {
    runtime.sensorDiff = fabs(runtime.sensorA.temperature - runtime.sensorB.temperature);
    runtime.controlTemp = (runtime.sensorA.temperature + runtime.sensorB.temperature) * 0.5f;
    runtime.alarms.sensorMismatch = runtime.sensorDiff > runtime.sensorDiffAlarm;
  } else if (runtime.sensorA.valid) {
    runtime.sensorDiff = NAN;
    runtime.controlTemp = runtime.sensorA.temperature;
    runtime.alarms.sensorMismatch = false;
  } else if (runtime.sensorB.valid) {
    runtime.sensorDiff = NAN;
    runtime.controlTemp = runtime.sensorB.temperature;
    runtime.alarms.sensorMismatch = false;
  } else {
    runtime.sensorDiff = NAN;
    runtime.controlTemp = NAN;
    runtime.alarms.sensorMismatch = false;
  }

  runtime.sensorConversionPending = false;
  runtime.lastSampleMs = millis();
  if (runtime.lastSampleReadyMs != 0) {
    runtime.lastSampleCycleMs = runtime.lastSampleMs - runtime.lastSampleReadyMs;
    if (runtime.lastSampleCycleMs > runtime.maxSampleCycleMs) {
      runtime.maxSampleCycleMs = runtime.lastSampleCycleMs;
    }
  }
  runtime.lastSampleReadyMs = runtime.lastSampleMs;
}

void updateStateMachine() {
  SystemState previousState = runtime.state;
  runtime.alarms.wifiDisconnected = WiFi.status() != WL_CONNECTED;
  runtime.alarms.faultLatched = runtime.faultLatched;
  updateSafetyCutoffs();

  if (runtime.faultLatched) {
    stopAllOutputs();
    runtime.alarms.faultLatched = true;
    runtime.state = SystemState::FaultStop;
    if (runtime.alarms.highTempCutoff) {
      setLastFault(FaultCode::HighTempCutoff);
    } else if (runtime.alarms.lowTempCutoff) {
      setLastFault(FaultCode::LowTempCutoff);
    }
    return;
  }

  if (runtime.alarms.highTempCutoff) {
    enterLatchedFault(FaultCode::HighTempCutoff, previousState,
                     String("Safety cutoff: control temp too high, value=") + formatFloat(runtime.controlTemp));
    return;
  }

  if (runtime.alarms.lowTempCutoff) {
    enterLatchedFault(FaultCode::LowTempCutoff, previousState,
                     String("Safety cutoff: control temp too low, value=") + formatFloat(runtime.controlTemp));
    return;
  }

  if (runtime.alarms.bothSensorsFailed) {
    setLastFault(FaultCode::BothSensorsFailed);
  } else if (runtime.alarms.sensorAFailed) {
    setLastFault(FaultCode::SensorAFailed);
  } else if (runtime.alarms.sensorBFailed) {
    setLastFault(FaultCode::SensorBFailed);
  } else if (runtime.alarms.sensorMismatch) {
    setLastFault(FaultCode::SensorMismatch);
  } else if (runtime.alarms.wifiDisconnected) {
    setLastFault(FaultCode::WiFiDisconnected);
  } else {
    setLastFault(FaultCode::None);
  }

  if (runtime.alarms.bothSensorsFailed) {
    stopAllOutputs();
    runtime.state = SystemState::FaultStop;
    if (previousState != SystemState::FaultStop) {
      runtime.faultStopCount++;
      pulseBuzzer(ControlConfig::BUZZ_PULSE_MS * 3);
      logEvent(String("State -> ") + stateToText(runtime.state));
    }
    return;
  }

  if (!runtime.sensorA.valid || !runtime.sensorB.valid) {
    runtime.state = SystemState::Degraded;
  } else if (runtime.alarms.sensorMismatch || runtime.alarms.wifiDisconnected) {
    runtime.state = SystemState::Alarm;
  } else if (!runtime.heatOn && !runtime.coolOn) {
    runtime.state = SystemState::Idle;
  }

  if (isnan(runtime.controlTemp)) {
    stopAllOutputs();
    runtime.state = SystemState::FaultStop;
    setLastFault(FaultCode::ControlTempInvalid);
    if (previousState != SystemState::FaultStop) {
      runtime.faultStopCount++;
      pulseBuzzer(ControlConfig::BUZZ_PULSE_MS * 3);
      logEvent(String("State -> ") + stateToText(runtime.state));
    }
    return;
  }

  if (outputsStartupInhibited()) {
    stopAllOutputs();
    if (runtime.state != SystemState::Degraded && runtime.state != SystemState::Alarm) {
      runtime.state = SystemState::Idle;
    }
    if (runtime.state != previousState) {
      logEvent(String("State -> ") + stateToText(runtime.state));
    }
    return;
  }

  float lowerLimit = runtime.setpoint - runtime.hysteresis;
  float upperLimit = runtime.setpoint + runtime.hysteresis;

  if (runtime.controlTemp < lowerLimit) {
    if (runtime.coolOn && millis() - runtime.coolStateChangedMs >= ControlConfig::COOL_MIN_ON_MS) {
      setCoolOutput(false);
    }
    if (!runtime.coolOn) {
      if (!runtime.heatOn) {
        setHeatOutput(true);
      }
      runtime.state = runtime.state == SystemState::Degraded ? SystemState::Degraded : SystemState::Heating;
    }
    return;
  }

  if (runtime.controlTemp > upperLimit) {
    if (runtime.heatOn && millis() - runtime.heatStateChangedMs >= ControlConfig::HEAT_MIN_ON_MS) {
      setHeatOutput(false);
    }
    if (!runtime.heatOn) {
      if (!coolingProtected() && !runtime.coolOn) {
        setCoolOutput(true);
      }
      if (runtime.coolOn) {
        runtime.state = runtime.state == SystemState::Degraded ? SystemState::Degraded : SystemState::Cooling;
      }
    }
    return;
  }

  if (runtime.heatOn && millis() - runtime.heatStateChangedMs >= ControlConfig::HEAT_MIN_ON_MS) {
    setHeatOutput(false);
  }
  if (runtime.coolOn && millis() - runtime.coolStateChangedMs >= ControlConfig::COOL_MIN_ON_MS) {
    setCoolOutput(false);
  }

  if (runtime.state != SystemState::Degraded && runtime.state != SystemState::Alarm) {
    runtime.state = SystemState::Idle;
  }

  if (runtime.state != previousState) {
    if (runtime.state == SystemState::Alarm) {
      runtime.alarmCount++;
    } else if (runtime.state == SystemState::Degraded) {
      runtime.degradedCount++;
    }
    logEvent(String("State -> ") + stateToText(runtime.state));
  }
}

void handleButtons() {
  if (millis() - runtime.lastButtonMs < ControlConfig::BUTTON_SCAN_INTERVAL_MS) {
    return;
  }
  runtime.lastButtonMs = millis();

  bool setPressed = readKeyPressed(PinMap::KEY_SET);
  bool upPressed = readKeyPressed(PinMap::KEY_UP);
  bool downPressed = readKeyPressed(PinMap::KEY_DOWN);

  bool faultResetCombo = setPressed && upPressed && !downPressed;
  bool statsResetCombo = setPressed && downPressed && !upPressed;

  if (setPressed && !runtime.keySetPrev) {
    runtime.keySetPressedMs = millis();
    runtime.maintenanceComboHandled = false;
    runtime.keySetComboSeen = false;
  }

  if (setPressed && (upPressed || downPressed)) {
    runtime.keySetComboSeen = true;
  }

  if (!setPressed) {
    runtime.maintenanceComboHandled = false;
  }

  if (!runtime.localMenuActive && !runtime.maintenanceComboHandled &&
      millis() - runtime.keySetPressedMs >= ControlConfig::LONG_PRESS_MS) {
    if (faultResetCombo) {
      clearFaultLatch();
      pulseBuzzer(ControlConfig::BUZZ_PULSE_MS * 2);
      runtime.maintenanceComboHandled = true;
    } else if (statsResetCombo) {
      clearRuntimeStatistics();
      pulseBuzzer(ControlConfig::BUZZ_PULSE_MS * 2);
      runtime.maintenanceComboHandled = true;
    }
  }

  if (!setPressed && runtime.keySetPrev) {
    if (runtime.maintenanceComboHandled) {
      runtime.keySetPrev = setPressed;
      runtime.keyUpPrev = upPressed;
      runtime.keyDownPrev = downPressed;
      return;
    }

    uint32_t pressDuration = millis() - runtime.keySetPressedMs;
    if (pressDuration < ControlConfig::LONG_PRESS_MS && !runtime.keySetComboSeen) {
      runtime.buzzerEnabled = !runtime.buzzerEnabled;
      markSettingsDirty();
      pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
    }
  }

  if (upPressed && !runtime.keyUpPrev && !setPressed && !downPressed) {
    runtime.keyUpPressedMs = millis();
    runtime.keyUpLongAdjustActive = false;
    runtime.setpoint = clampFloat(runtime.setpoint + 0.1f, ControlConfig::MIN_SETPOINT, ControlConfig::MAX_SETPOINT);
    markSettingsDirty();
    pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  }

  if (downPressed && !runtime.keyDownPrev && !setPressed && !upPressed) {
    runtime.keyDownPressedMs = millis();
    runtime.keyDownLongAdjustActive = false;
    runtime.setpoint = clampFloat(runtime.setpoint - 0.1f, ControlConfig::MIN_SETPOINT, ControlConfig::MAX_SETPOINT);
    markSettingsDirty();
    pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  }

  if (upPressed && !setPressed && !downPressed &&
      millis() - runtime.keyUpPressedMs >= ControlConfig::LONG_PRESS_MS) {
    if (!runtime.keyUpLongAdjustActive) {
      runtime.keyUpLongAdjustActive = true;
      runtime.lastFastAdjustMs = millis();
    }
    if (millis() - runtime.lastFastAdjustMs >= ControlConfig::FAST_ADJUST_INTERVAL_MS) {
      runtime.setpoint = clampFloat(runtime.setpoint + 0.1f, ControlConfig::MIN_SETPOINT, ControlConfig::MAX_SETPOINT);
      runtime.fastAdjustPendingCommit = true;
      runtime.lastFastAdjustMs = millis();
    }
  }

  if (downPressed && !setPressed && !upPressed &&
      millis() - runtime.keyDownPressedMs >= ControlConfig::LONG_PRESS_MS) {
    if (!runtime.keyDownLongAdjustActive) {
      runtime.keyDownLongAdjustActive = true;
      runtime.lastFastAdjustMs = millis();
    }
    if (millis() - runtime.lastFastAdjustMs >= ControlConfig::FAST_ADJUST_INTERVAL_MS) {
      runtime.setpoint = clampFloat(runtime.setpoint - 0.1f, ControlConfig::MIN_SETPOINT, ControlConfig::MAX_SETPOINT);
      runtime.fastAdjustPendingCommit = true;
      runtime.lastFastAdjustMs = millis();
    }
  }

  if ((!upPressed && runtime.keyUpPrev && runtime.keyUpLongAdjustActive) ||
      (!downPressed && runtime.keyDownPrev && runtime.keyDownLongAdjustActive)) {
    runtime.keyUpLongAdjustActive = false;
    runtime.keyDownLongAdjustActive = false;
    if (runtime.fastAdjustPendingCommit) {
      markSettingsDirty();
      pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
      runtime.fastAdjustPendingCommit = false;
    }
  }

  runtime.localMenuActive = false;
  runtime.showSecondPage = false;

  runtime.keySetPrev = setPressed;
  runtime.keyUpPrev = upPressed;
  runtime.keyDownPrev = downPressed;
}

void updateDisplay() {
  updateRuntimeTotals();

  if (millis() - runtime.lastDisplayMs < ControlConfig::DISPLAY_INTERVAL_MS) {
    return;
  }
  runtime.lastDisplayMs = millis();

  display.clearDisplay();
  display.setTextColor(OLED_COLOR_WHITE);
  drawDisplayPageOne();
  display.display();
}

void initPins() {
  pinMode(PinMap::HEAT_CTRL, OUTPUT);
  pinMode(PinMap::COOL_CTRL, OUTPUT);
  pinMode(PinMap::BUZZ_CTRL, OUTPUT);
  pinMode(PinMap::KEY_SET, INPUT_PULLUP);
  pinMode(PinMap::KEY_UP, INPUT_PULLUP);
  pinMode(PinMap::KEY_DOWN, INPUT_PULLUP);
  stopAllOutputs();
  setBuzzer(false);
}

void initDisplay() {
  Wire.begin(PinMap::OLED_SDA, PinMap::OLED_SCL);
#if defined(OLED_DRIVER_SH1106) && OLED_DRIVER_SH1106
  if (!display.begin(DisplayConfig::I2C_ADDRESS, true)) {
    logEvent("OLED init failed (SH1106)");
    return;
  }
#else
  if (!display.begin(SSD1306_SWITCHCAPVCC, DisplayConfig::I2C_ADDRESS)) {
    logEvent("OLED init failed (SSD1306)");
    return;
  }
#endif
  display.clearDisplay();
  display.setTextColor(OLED_COLOR_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Fish Tank Ctrl");
  display.println("Init...");
  display.display();
}

void initSensors() {
  sensorBusA.begin();
  sensorBusB.begin();
  sensorBusA.setWaitForConversion(false);
  sensorBusB.setWaitForConversion(false);
  sensorBusA.setResolution(12);
  sensorBusB.setResolution(12);
  sensorBusA.requestTemperatures();
  sensorBusB.requestTemperatures();
  runtime.sensorRequestMs = millis();
  runtime.sensorConversionPending = true;
}

void clearFaultLatch() {
  runtime.faultLatched = false;
  runtime.alarms.faultLatched = false;
  runtime.alarms.lowTempCutoff = false;
  runtime.alarms.highTempCutoff = false;
  runtime.lastFaultCode = FaultCode::None;
  runtime.lastFaultText = faultCodeToText(FaultCode::None);
  if (runtime.state == SystemState::FaultStop) {
    runtime.state = SystemState::Idle;
  }
  logEvent("Fault latch cleared by user");
}

void clearRuntimeStatistics() {
  runtime.totalHeatOnMs = 0;
  runtime.totalCoolOnMs = 0;
  runtime.alarmCount = 0;
  runtime.degradedCount = 0;
  runtime.faultStopCount = 0;
  runtime.heatRelaySwitchCount = 0;
  runtime.coolRelaySwitchCount = 0;
  runtime.relayStatsDirty = false;
  updateRelayWearWarnings();
  preferences.putUInt("heat_sw", runtime.heatRelaySwitchCount);
  preferences.putUInt("cool_sw", runtime.coolRelaySwitchCount);
  runtime.lastStatsUpdateMs = millis();
  logEvent("Runtime statistics cleared by user");
}