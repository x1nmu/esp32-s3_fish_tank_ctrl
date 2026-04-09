#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#if defined(OLED_DRIVER_SH1106) && OLED_DRIVER_SH1106
#include <Adafruit_SH110X.h>
#else
#include <Adafruit_SSD1306.h>
#endif
#include <Preferences.h>

#if defined(OLED_DRIVER_SH1106) && OLED_DRIVER_SH1106
using OledDisplayType = Adafruit_SH1106G;
constexpr uint16_t OLED_COLOR_WHITE = SH110X_WHITE;
#else
using OledDisplayType = Adafruit_SSD1306;
constexpr uint16_t OLED_COLOR_WHITE = SSD1306_WHITE;
#endif

namespace PinMap {
#if CONFIG_IDF_TARGET_ESP32S3
constexpr uint8_t TEMP_A = 4;
constexpr uint8_t TEMP_B = 16;
constexpr uint8_t COOL_CTRL = 18;
constexpr uint8_t HEAT_CTRL = 19;
constexpr uint8_t OLED_SDA = 8;
constexpr uint8_t OLED_SCL = 9;
constexpr uint8_t BUZZ_CTRL = 15;
constexpr uint8_t KEY_SET = 12;
constexpr uint8_t KEY_UP = 13;
constexpr uint8_t KEY_DOWN = 14;
#else
constexpr uint8_t TEMP_A = 4;
constexpr uint8_t TEMP_B = 16;
constexpr uint8_t COOL_CTRL = 18;
constexpr uint8_t HEAT_CTRL = 19;
constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t BUZZ_CTRL = 23;
constexpr uint8_t KEY_SET = 25;
constexpr uint8_t KEY_UP = 26;
constexpr uint8_t KEY_DOWN = 27;
#endif
}

namespace DisplayConfig {
constexpr uint8_t WIDTH = 128;
constexpr uint8_t HEIGHT = 64;
constexpr int8_t RESET_PIN = -1;
#ifdef OLED_I2C_ADDRESS
constexpr uint8_t I2C_ADDRESS = OLED_I2C_ADDRESS;
#else
constexpr uint8_t I2C_ADDRESS = 0x3C;
#endif
}

namespace ControlConfig {
constexpr float DEFAULT_SETPOINT = 26.0f;
constexpr float DEFAULT_HYSTERESIS = 0.5f;
constexpr float DEFAULT_SENSOR_DIFF_ALARM = 1.0f;
constexpr float ABS_LOW_TEMP_CUTOFF = 18.0f;
constexpr float ABS_HIGH_TEMP_CUTOFF = 34.0f;
constexpr float MIN_SETPOINT = 20.0f;
constexpr float MAX_SETPOINT = 32.0f;
constexpr float MIN_HYSTERESIS = 0.3f;
constexpr float MAX_HYSTERESIS = 2.0f;
constexpr float MIN_SENSOR_DIFF_ALARM = 0.3f;
constexpr float MAX_SENSOR_DIFF_ALARM = 3.0f;
constexpr float MIN_VALID_TEMP = 0.0f;
constexpr float MAX_VALID_TEMP = 50.0f;
constexpr float MAX_SENSOR_STEP_DELTA = 2.5f;
constexpr float SENSOR_FILTER_ALPHA = 0.35f;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint32_t SENSOR_POLL_INTERVAL_MS = 50;
constexpr uint32_t DISPLAY_INTERVAL_MS = 66;
constexpr uint32_t DEBUG_PRINT_INTERVAL_MS = 5000;
constexpr uint32_t BUTTON_SCAN_INTERVAL_MS = 20;
constexpr uint32_t SENSOR_CONVERSION_MS = 760;
constexpr uint32_t BUZZ_PULSE_MS = 120;
constexpr uint32_t LONG_PRESS_MS = 1200;
constexpr uint32_t FAST_ADJUST_INTERVAL_MS = 100;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
constexpr uint32_t WIFI_AP_RETRY_INTERVAL_MS = 60000;
constexpr uint32_t WIFI_SAVE_RECONNECT_DELAY_MS = 1200;
constexpr uint32_t OUTPUT_STARTUP_INHIBIT_MS = 10000;
constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 2000;
constexpr uint32_t RELAY_STATS_SAVE_DELAY_MS = 2000;
constexpr uint32_t HEAT_MIN_ON_MS = 30000;
constexpr uint32_t COOL_MIN_ON_MS = 60000;
constexpr uint32_t COOL_MIN_OFF_MS = 180000;
constexpr uint32_t RELAY_WARN_SWITCH_COUNT = 100000;
constexpr uint8_t SENSOR_FAIL_LIMIT = 3;
constexpr uint8_t SENSOR_RECOVER_VALID_COUNT = 2;
constexpr bool RELAY_ACTIVE_LEVEL = LOW;
constexpr bool BUZZER_ACTIVE_LEVEL = HIGH;
constexpr bool DEFAULT_BUZZER_ENABLED = true;
constexpr char DEFAULT_WIFI_SSID[] = "";
constexpr char DEFAULT_WIFI_PASSWORD[] = "";
constexpr char AP_SSID_PREFIX[] = "FishTankCtrl-Setup";
constexpr char AP_PASSWORD[] = "12345678";
constexpr size_t WIFI_SSID_MAX_LEN = 33;
constexpr size_t WIFI_PASSWORD_MAX_LEN = 65;
constexpr uint8_t EVENT_LOG_SIZE = 6;
}

enum class SystemState {
  Init,
  Idle,
  Heating,
  Cooling,
  Degraded,
  Alarm,
  FaultStop,
};

enum class FaultCode : uint8_t {
  None,
  SensorAFailed,
  SensorBFailed,
  BothSensorsFailed,
  SensorMismatch,
  WiFiDisconnected,
  ControlTempInvalid,
  LowTempCutoff,
  HighTempCutoff,
};

enum class MenuItem : uint8_t {
  Setpoint,
  Hysteresis,
  SensorDiffAlarm,
  Buzzer,
  Count,
};

struct SensorChannel {
  float temperature = NAN;
  float lastRawTemperature = NAN;
  bool valid = false;
  uint8_t failCount = 0;
  uint8_t recoverCount = 0;
};

struct AlarmFlags {
  bool sensorAFailed = false;
  bool sensorBFailed = false;
  bool bothSensorsFailed = false;
  bool sensorMismatch = false;
  bool wifiDisconnected = false;
  bool lowTempCutoff = false;
  bool highTempCutoff = false;
  bool faultLatched = false;
  bool heatRelayWearWarning = false;
  bool coolRelayWearWarning = false;
  bool compressorProtected = false;
  bool outputStartupInhibit = true;
};

struct RuntimeData {
  float setpoint = ControlConfig::DEFAULT_SETPOINT;
  float hysteresis = ControlConfig::DEFAULT_HYSTERESIS;
  float sensorDiffAlarm = ControlConfig::DEFAULT_SENSOR_DIFF_ALARM;
  float controlTemp = NAN;
  float sensorDiff = NAN;
  SensorChannel sensorA;
  SensorChannel sensorB;
  AlarmFlags alarms;
  SystemState state = SystemState::Init;
  bool heatOn = false;
  bool coolOn = false;
  bool buzzerEnabled = ControlConfig::DEFAULT_BUZZER_ENABLED;
  bool showSecondPage = false;
  bool localMenuActive = false;
  bool wifiReconnectRequested = false;
  bool wifiApMode = false;
  bool settingsDirty = false;
  bool relayStatsDirty = false;
  bool maintenanceComboHandled = false;
  bool keySetComboSeen = false;
  bool keySetPrev = false;
  bool keyUpPrev = false;
  bool keyDownPrev = false;
  uint8_t menuIndex = 0;
  uint32_t lastSampleMs = 0;
  uint32_t lastDisplayMs = 0;
  uint32_t lastButtonMs = 0;
  uint32_t lastDebugMs = 0;
  uint32_t lastSettingsChangedMs = 0;
  uint32_t lastRelayStatsChangedMs = 0;
  uint32_t heatStateChangedMs = 0;
  uint32_t coolStateChangedMs = 0;
  uint32_t lastStatsUpdateMs = 0;
  uint32_t buzzerOffMs = 0;
  uint32_t bootMs = 0;
  uint32_t keySetPressedMs = 0;
  uint32_t keyUpPressedMs = 0;
  uint32_t keyDownPressedMs = 0;
  uint32_t lastFastAdjustMs = 0;
  uint32_t lastWiFiRetryMs = 0;
  uint32_t wifiConnectStartedMs = 0;
  uint32_t wifiReconnectDueMs = 0;
  uint32_t sensorRequestMs = 0;
  uint32_t lastSensorPollMs = 0;
  uint32_t lastSampleCycleMs = 0;
  uint32_t maxSampleCycleMs = 0;
  uint32_t lastSampleReadyMs = 0;
  char wifiSsid[ControlConfig::WIFI_SSID_MAX_LEN] = {0};
  char wifiPassword[ControlConfig::WIFI_PASSWORD_MAX_LEN] = {0};
  char wifiApSsid[32] = {0};
  uint32_t wifiApStartedMs = 0;
  uint32_t totalHeatOnMs = 0;
  uint32_t totalCoolOnMs = 0;
  uint16_t alarmCount = 0;
  uint16_t degradedCount = 0;
  uint16_t faultStopCount = 0;
  uint32_t heatRelaySwitchCount = 0;
  uint32_t coolRelaySwitchCount = 0;
  bool faultLatched = false;
  bool sensorConversionPending = false;
  bool keyUpLongAdjustActive = false;
  bool keyDownLongAdjustActive = false;
  bool fastAdjustPendingCommit = false;
  FaultCode lastFaultCode = FaultCode::None;
  String lastFaultText = "NONE";
  String eventLog[ControlConfig::EVENT_LOG_SIZE];
  uint8_t eventLogCount = 0;
  uint8_t eventLogHead = 0;
};

extern OneWire oneWireA;
extern OneWire oneWireB;
extern DallasTemperature sensorBusA;
extern DallasTemperature sensorBusB;
extern OledDisplayType display;
extern WebServer server;
extern Preferences preferences;
extern RuntimeData runtime;

String stateToText(SystemState state);
String faultCodeToText(FaultCode code);
String faultCodeToDescription(FaultCode code);
String formatFloat(float value, uint8_t digits = 1);
String menuItemLabel(MenuItem item);
String menuItemValue(MenuItem item);
void logEvent(const String& message);
void markSettingsDirty();
void pulseBuzzer(uint32_t durationMs);