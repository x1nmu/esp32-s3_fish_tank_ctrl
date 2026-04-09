#pragma once

#include "app_context.h"

String currentIpAddress();
String currentWiFiLabel();
void setupWebServer();
void connectWiFi(bool forceReconnect = false);
void maintainWiFi();