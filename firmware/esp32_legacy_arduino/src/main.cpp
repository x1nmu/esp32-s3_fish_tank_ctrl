#include "app_context.h"
#include "app_network.h"
#include "app_runtime.h"

OneWire oneWireA(PinMap::TEMP_A);
OneWire oneWireB(PinMap::TEMP_B);
DallasTemperature sensorBusA(&oneWireA);
DallasTemperature sensorBusB(&oneWireB);
OledDisplayType display(DisplayConfig::WIDTH, DisplayConfig::HEIGHT, &Wire, DisplayConfig::RESET_PIN);
WebServer server(80);
Preferences preferences;
RuntimeData runtime;

static void logHardwareConfig() {
  uint32_t flashBytes = ESP.getFlashChipSize();
  bool psramOk = psramFound();
  uint32_t psramBytes = ESP.getPsramSize();
  uint32_t psramFree = ESP.getFreePsram();
  uint32_t heapFree = ESP.getFreeHeap();
  uint32_t heapMax = ESP.getMaxAllocHeap();

  Serial.println("==== HW CONFIG CHECK ====");
  Serial.printf("Flash: %lu bytes (%.1f MB)\n", static_cast<unsigned long>(flashBytes), flashBytes / (1024.0f * 1024.0f));
  Serial.printf("PSRAM: found=%s size=%lu bytes (%.1f MB) free=%lu bytes\n",
                psramOk ? "yes" : "no",
                static_cast<unsigned long>(psramBytes),
                psramBytes / (1024.0f * 1024.0f),
                static_cast<unsigned long>(psramFree));
  Serial.printf("Heap: free=%lu bytes maxAlloc=%lu bytes\n",
                static_cast<unsigned long>(heapFree),
                static_cast<unsigned long>(heapMax));

#if defined(ARDUINO_PARTITION_default_16MB)
  Serial.println("Partition: default_16MB");
#elif defined(ARDUINO_PARTITION_default_8MB)
  Serial.println("Partition: default_8MB");
#elif defined(ARDUINO_PARTITION_huge_app)
  Serial.println("Partition: huge_app");
#else
  Serial.println("Partition: unknown (check PlatformIO build flags)");
#endif
  Serial.println("=========================");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  logHardwareConfig();
  runtime.bootMs = millis();
  logEvent("Booting fish tank controller");
  loadSettings();
  initPins();
  initDisplay();
  initSensors();
  connectWiFi(true);
  setupWebServer();
  runtime.state = SystemState::Idle;
  runtime.coolStateChangedMs = millis();
  runtime.heatStateChangedMs = millis();
  logEvent("Setup finished");
}

void loop() {
  server.handleClient();
  handleButtons();
  maintainWiFi();

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

  saveSettingsIfNeeded();
  logPeriodicStatus();
  updateDisplay();
}
