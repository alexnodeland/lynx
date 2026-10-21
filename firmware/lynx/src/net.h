// SPDX-License-Identifier: MIT
// Wi-Fi: first-boot setup hotspot with a captive portal, station mode, mDNS, and the BOOT-button
// way back into setup.
#pragma once
#include <Arduino.h>

namespace lynx {

void netBegin();
void netLoop();

String hostName();  // "lynx-1a2b" (last two MAC bytes): two units on one network don't collide
String mdnsName();  // "lynx-1a2b.local"
String deviceId();  // full MAC, 12 lowercase hex: the stable identity (Home Assistant unique_id)
String staIp();     // empty until joined
IPAddress apIp();
bool setupMode();   // the setup hotspot is up
uint32_t setupClosesInMs();  // 0 = may close now / not open

// Start joining `ssid` (saved to NVS). Safe from any task. The setup page polls wifiState().
void joinWifi(const String& ssid, const String& pass);
const char* wifiState();  // "idle" | "connecting" | "connected" | "failed"

// Bring the setup hotspot up for at least `ms` (BOOT held 3 s, serial `setup`). Safe from any task.
void enterSetupMode(uint32_t ms);

}  // namespace lynx
