#pragma once

#include "app_context.h"

float clampFloat(float value, float minValue, float maxValue);
void logPeriodicStatus();
void loadSettings();
void saveSettingsIfNeeded();
void initPins();
void initDisplay();
void initSensors();
void handleButtons();
void updateSensors();
void updateStateMachine();
void updateDisplay();
void setBuzzer(bool enabled);
void pulseBuzzer(uint32_t durationMs);
void clearFaultLatch();
void clearRuntimeStatistics();