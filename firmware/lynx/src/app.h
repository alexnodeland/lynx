// SPDX-License-Identifier: MIT
// State shared by the main loop and the HTTP server tasks.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include <string>

#include "motion.h"
#include "settings.h"

namespace lynx {

constexpr const char* kVersion = "0.3.0";

extern Settings settings;
extern Motion motion;  // touch only while holding a MotionLock: the HTTP servers run in their own tasks

class MotionLock {
 public:
  MotionLock();
  ~MotionLock();
  MotionLock(const MotionLock&) = delete;
  MotionLock& operator=(const MotionLock&) = delete;
};

void lockBegin();

// Status for the API, MCP and serial. Takes the lock itself.
void statusJson(JsonObject j);

// Apply {framesize, quality, vflip, hmirror, brightness}; omitted fields stay. Saves to NVS.
bool applyCameraJson(JsonObjectConst in, std::string& error);
void cameraJson(JsonObject out);

}  // namespace lynx
