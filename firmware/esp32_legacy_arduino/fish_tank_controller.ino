#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SSD1306.h>

#ifndef OLED_DRIVER_SH1106
#define OLED_DRIVER_SH1106 1
#endif

#if OLED_DRIVER_SH1106
using OledDisplayType = Adafruit_SH1106G;
constexpr uint16_t OLED_COLOR_WHITE = SH110X_WHITE;
#else
using OledDisplayType = Adafruit_SSD1306;
constexpr uint16_t OLED_COLOR_WHITE = SSD1306_WHITE;
#endif

namespace PinMap {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
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
constexpr uint8_t I2C_ADDRESS = 0x3C;
}

namespace ControlConfig {
constexpr float DEFAULT_SETPOINT = 26.0f;
constexpr float DEFAULT_HYSTERESIS = 0.5f;
constexpr float SENSOR_DIFF_ALARM = 1.0f;
constexpr float MIN_VALID_TEMP = 0.0f;
constexpr float MAX_VALID_TEMP = 50.0f;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint32_t SENSOR_POLL_INTERVAL_MS = 50;
constexpr uint32_t DISPLAY_INTERVAL_MS = 500;
constexpr uint32_t WEB_REFRESH_INTERVAL_MS = 1000;
constexpr uint32_t BUTTON_SCAN_INTERVAL_MS = 20;
constexpr uint32_t SENSOR_CONVERSION_MS = 760;
constexpr uint32_t DEBUG_PRINT_INTERVAL_MS = 5000;
constexpr uint32_t BUZZ_PULSE_MS = 120;
constexpr uint32_t HEAT_MIN_ON_MS = 30000;
constexpr uint32_t COOL_MIN_ON_MS = 60000;
constexpr uint32_t COOL_MIN_OFF_MS = 180000;
constexpr uint8_t SENSOR_FAIL_LIMIT = 3;
constexpr bool RELAY_ACTIVE_LEVEL = LOW;
constexpr bool BUZZER_ACTIVE_LEVEL = HIGH;
constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
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

struct SensorChannel {
  float temperature = NAN;
  bool valid = false;
  uint8_t failCount = 0;
};

struct AlarmFlags {
  bool sensorAFailed = false;
  bool sensorBFailed = false;
  bool bothSensorsFailed = false;
  bool sensorMismatch = false;
  bool wifiDisconnected = false;
  bool compressorProtected = false;
};

struct RuntimeData {
  float setpoint = ControlConfig::DEFAULT_SETPOINT;
  float hysteresis = ControlConfig::DEFAULT_HYSTERESIS;
  float controlTemp = NAN;
  float sensorDiff = NAN;
  SensorChannel sensorA;
  SensorChannel sensorB;
  AlarmFlags alarms;
  SystemState state = SystemState::Init;
  bool heatOn = false;
  bool coolOn = false;
  bool showSecondPage = false;
  uint32_t lastSampleMs = 0;
  uint32_t lastSampleCycleMs = 0;
  uint32_t maxSampleCycleMs = 0;
  uint32_t lastSampleReadyMs = 0;
  uint32_t lastSensorPollMs = 0;
  uint32_t lastDisplayMs = 0;
  uint32_t lastButtonMs = 0;
  uint32_t lastDebugMs = 0;
  uint32_t lastWebMs = 0;
  uint32_t sensorRequestMs = 0;
  uint32_t heatStateChangedMs = 0;
  uint32_t coolStateChangedMs = 0;
  uint32_t buzzerOffMs = 0;
  bool sensorConversionPending = false;
  bool keySetPrev = false;
  bool keyUpPrev = false;
  bool keyDownPrev = false;
};

OneWire oneWireA(PinMap::TEMP_A);
OneWire oneWireB(PinMap::TEMP_B);
DallasTemperature sensorBusA(&oneWireA);
DallasTemperature sensorBusB(&oneWireB);
OledDisplayType display(DisplayConfig::WIDTH, DisplayConfig::HEIGHT, &Wire, DisplayConfig::RESET_PIN);
WebServer server(80);
RuntimeData runtime;

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

String formatFloat(float value, uint8_t digits = 1) {
  if (isnan(value)) {
    return "--";
  }
  return String(value, digits);
}

bool relayLevelFor(bool enabled) {
  return enabled ? ControlConfig::RELAY_ACTIVE_LEVEL : !ControlConfig::RELAY_ACTIVE_LEVEL;
}

void setHeatOutput(bool enabled) {
  runtime.heatOn = enabled;
  digitalWrite(PinMap::HEAT_CTRL, relayLevelFor(enabled));
  runtime.heatStateChangedMs = millis();
}

void setCoolOutput(bool enabled) {
  runtime.coolOn = enabled;
  digitalWrite(PinMap::COOL_CTRL, relayLevelFor(enabled));
  runtime.coolStateChangedMs = millis();
}

void setBuzzer(bool enabled) {
  digitalWrite(PinMap::BUZZ_CTRL, enabled ? ControlConfig::BUZZER_ACTIVE_LEVEL : !ControlConfig::BUZZER_ACTIVE_LEVEL);
}

void pulseBuzzer(uint32_t durationMs) {
  setBuzzer(true);
  runtime.buzzerOffMs = millis() + durationMs;
}

void stopAllOutputs() {
  setHeatOutput(false);
  setCoolOutput(false);
}

bool readKeyPressed(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

bool isTemperaturePlausible(float temp) {
  return temp >= ControlConfig::MIN_VALID_TEMP && temp <= ControlConfig::MAX_VALID_TEMP;
}

void updateSensorChannel(DallasTemperature& bus, SensorChannel& channel) {
  float value = bus.getTempCByIndex(0);
  bool valid = value != DEVICE_DISCONNECTED_C && isTemperaturePlausible(value);

  if (valid) {
    channel.temperature = value;
    channel.valid = true;
    channel.failCount = 0;
  } else {
    channel.valid = false;
    channel.failCount = min<uint8_t>(255, channel.failCount + 1);
    if (channel.failCount >= ControlConfig::SENSOR_FAIL_LIMIT) {
      channel.temperature = NAN;
    }
  }
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

  updateSensorChannel(sensorBusA, runtime.sensorA);
  updateSensorChannel(sensorBusB, runtime.sensorB);

  runtime.alarms.sensorAFailed = !runtime.sensorA.valid && runtime.sensorA.failCount >= ControlConfig::SENSOR_FAIL_LIMIT;
  runtime.alarms.sensorBFailed = !runtime.sensorB.valid && runtime.sensorB.failCount >= ControlConfig::SENSOR_FAIL_LIMIT;
  runtime.alarms.bothSensorsFailed = runtime.alarms.sensorAFailed && runtime.alarms.sensorBFailed;

  if (runtime.sensorA.valid && runtime.sensorB.valid) {
    runtime.sensorDiff = fabs(runtime.sensorA.temperature - runtime.sensorB.temperature);
    runtime.controlTemp = (runtime.sensorA.temperature + runtime.sensorB.temperature) * 0.5f;
    runtime.alarms.sensorMismatch = runtime.sensorDiff > ControlConfig::SENSOR_DIFF_ALARM;
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

void updateStateMachine() {
  runtime.alarms.wifiDisconnected = WiFi.status() != WL_CONNECTED;

  if (runtime.alarms.bothSensorsFailed) {
    stopAllOutputs();
    runtime.state = SystemState::FaultStop;
    pulseBuzzer(ControlConfig::BUZZ_PULSE_MS * 3);
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
}

void handleButtons() {
  if (millis() - runtime.lastButtonMs < ControlConfig::BUTTON_SCAN_INTERVAL_MS) {
    return;
  }
  runtime.lastButtonMs = millis();

  bool upPressed = readKeyPressed(PinMap::KEY_UP);
  bool downPressed = readKeyPressed(PinMap::KEY_DOWN);
  bool setPressed = readKeyPressed(PinMap::KEY_SET);

  if (upPressed && !runtime.keyUpPrev) {
    runtime.setpoint = min(32.0f, runtime.setpoint + 0.5f);
    pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  }
  if (downPressed && !runtime.keyDownPrev) {
    runtime.setpoint = max(20.0f, runtime.setpoint - 0.5f);
    pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  }
  if (setPressed && !runtime.keySetPrev) {
    runtime.showSecondPage = !runtime.showSecondPage;
    pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  }

  runtime.keySetPrev = setPressed;
  runtime.keyUpPrev = upPressed;
  runtime.keyDownPrev = downPressed;
}

void drawDisplayPageOne() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("T1:");
  display.print(formatFloat(runtime.sensorA.temperature));
  display.print("C");

  display.setCursor(0, 12);
  display.print("T2:");
  display.print(formatFloat(runtime.sensorB.temperature));
  display.print("C");

  display.setCursor(0, 24);
  display.print("TC:");
  display.print(formatFloat(runtime.controlTemp));
  display.print("C");

  display.setCursor(0, 36);
  display.print("SET:");
  display.print(formatFloat(runtime.setpoint));
  display.print("C");

  display.setCursor(0, 48);
  display.print("MODE:");
  display.print(stateToText(runtime.state));
}

void drawDisplayPageTwo() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("HEAT:");
  display.print(runtime.heatOn ? "ON" : "OFF");

  display.setCursor(0, 12);
  display.print("COOL:");
  display.print(runtime.coolOn ? "ON" : "OFF");

  display.setCursor(0, 24);
  display.print("ALM:");
  display.print((runtime.alarms.sensorMismatch || runtime.alarms.sensorAFailed || runtime.alarms.sensorBFailed) ? "YES" : "NO");

  display.setCursor(0, 36);
  display.print("WIFI:");
  display.print(WiFi.status() == WL_CONNECTED ? "OK" : "DISC");

  display.setCursor(0, 48);
  display.print("IP:");
  display.print(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "--");
}

void updateDisplay() {
  if (millis() - runtime.lastDisplayMs < ControlConfig::DISPLAY_INTERVAL_MS) {
    return;
  }
  runtime.lastDisplayMs = millis();

  display.clearDisplay();
  display.setTextColor(OLED_COLOR_WHITE);
  if (runtime.showSecondPage) {
    drawDisplayPageTwo();
  } else {
    drawDisplayPageOne();
  }
  display.display();
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
  Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "DISC");
  Serial.print(" SAMPLE=");
  Serial.print(runtime.lastSampleCycleMs);
  Serial.print("ms MAX=");
  Serial.print(runtime.maxSampleCycleMs);
  Serial.print("ms CONV_AGE=");
  Serial.print(conversionAgeMs);
  Serial.println("ms");
}

String buildJsonStatus() {
  String json = "{";
  json += "\"state\":\"" + stateToText(runtime.state) + "\",";
  json += "\"setpoint\":" + String(runtime.setpoint, 1) + ",";
  json += "\"controlTemp\":" + (isnan(runtime.controlTemp) ? String("null") : String(runtime.controlTemp, 2)) + ",";
  json += "\"sensorA\":" + (isnan(runtime.sensorA.temperature) ? String("null") : String(runtime.sensorA.temperature, 2)) + ",";
  json += "\"sensorB\":" + (isnan(runtime.sensorB.temperature) ? String("null") : String(runtime.sensorB.temperature, 2)) + ",";
  json += "\"heatOn\":" + String(runtime.heatOn ? "true" : "false") + ",";
  json += "\"coolOn\":" + String(runtime.coolOn ? "true" : "false") + ",";
  json += "\"sensorMismatch\":" + String(runtime.alarms.sensorMismatch ? "true" : "false") + ",";
  json += "\"sensorAFailed\":" + String(runtime.alarms.sensorAFailed ? "true" : "false") + ",";
  json += "\"sensorBFailed\":" + String(runtime.alarms.sensorBFailed ? "true" : "false") + ",";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += "}";
  return json;
}

String buildHtmlPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>100L Fish Tank Controller</title>";
  html += "<style>body{font-family:Segoe UI,Arial,sans-serif;background:#eef6f7;color:#102a43;padding:20px;}";
  html += ".card{max-width:640px;margin:auto;background:#fff;border-radius:16px;padding:20px;box-shadow:0 10px 30px rgba(16,42,67,.12);}h1{margin-top:0;font-size:24px;}";
  html += ".grid{display:grid;grid-template-columns:repeat(2,minmax(120px,1fr));gap:12px;}";
  html += ".item{background:#f0f4f8;border-radius:12px;padding:12px;}small{display:block;color:#627d98;}strong{font-size:20px;}</style></head><body>";
  html += "<div class='card'><h1>100L 鱼缸温控器</h1><div class='grid'>";
  html += "<div class='item'><small>状态</small><strong>" + stateToText(runtime.state) + "</strong></div>";
  html += "<div class='item'><small>目标温度</small><strong>" + formatFloat(runtime.setpoint) + " C</strong></div>";
  html += "<div class='item'><small>控制温度</small><strong>" + formatFloat(runtime.controlTemp) + " C</strong></div>";
  html += "<div class='item'><small>探头差值</small><strong>" + formatFloat(runtime.sensorDiff) + " C</strong></div>";
  html += "<div class='item'><small>探头A</small><strong>" + formatFloat(runtime.sensorA.temperature) + " C</strong></div>";
  html += "<div class='item'><small>探头B</small><strong>" + formatFloat(runtime.sensorB.temperature) + " C</strong></div>";
  html += "<div class='item'><small>加热</small><strong>" + String(runtime.heatOn ? "ON" : "OFF") + "</strong></div>";
  html += "<div class='item'><small>制冷</small><strong>" + String(runtime.coolOn ? "ON" : "OFF") + "</strong></div>";
  html += "</div><p>WiFi: " + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("Disconnected")) + "</p></div></body></html>";
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
    server.send(400, "text/plain", "missing value");
    return;
  }
  float newValue = server.arg("value").toFloat();
  if (newValue < 20.0f || newValue > 32.0f) {
    server.send(400, "text/plain", "setpoint out of range");
    return;
  }
  runtime.setpoint = newValue;
  pulseBuzzer(ControlConfig::BUZZ_PULSE_MS);
  server.send(200, "application/json", buildJsonStatus());
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/set", HTTP_POST, handleSetpoint);
  server.begin();
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ControlConfig::WIFI_SSID, ControlConfig::WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(250);
  }
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
#if OLED_DRIVER_SH1106
  display.begin(DisplayConfig::I2C_ADDRESS, true);
#else
  display.begin(SSD1306_SWITCHCAPVCC, DisplayConfig::I2C_ADDRESS);
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

void setup() {
  Serial.begin(115200);
  initPins();
  initDisplay();
  initSensors();
  connectWiFi();
  setupWebServer();
  runtime.state = SystemState::Idle;
  runtime.coolStateChangedMs = millis();
  runtime.heatStateChangedMs = millis();
}

void loop() {
  server.handleClient();
  handleButtons();

  if (millis() - runtime.lastSensorPollMs >= ControlConfig::SENSOR_POLL_INTERVAL_MS) {
    runtime.lastSensorPollMs = millis();
    updateSensors();
    if (!runtime.sensorConversionPending) {
      updateStateMachine();
    }
  }

  if (runtime.buzzerOffMs != 0 && millis() >= runtime.buzzerOffMs) {
    setBuzzer(false);
    runtime.buzzerOffMs = 0;
  }

  logPeriodicStatus();
  updateDisplay();
}