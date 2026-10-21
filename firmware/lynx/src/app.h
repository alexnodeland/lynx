// SPDX-License-Identifier: MIT
// State shared by the main loop and the HTTP server tasks.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include <string>

#include "motion.h"
#include "settings.h"

namespace lynx {

constexpr const char* kVersion = "0.4.0";

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

// Detection + follow, shared by REST, MCP and serial. Each takes the lock it needs and saves to NVS.
void detectionsJson(JsonObject out);  // the latest frame's boxes and a summary
bool applyDetectJson(JsonObjectConst in, std::string& error);  // {enabled, model}
bool applyFollowJson(JsonObjectConst in, std::string& error);  // {enabled, panSign, tiltSign}
// A manual move pauses follow for a few seconds, so the head goes where you asked.
void pauseFollow(uint32_t ms = 5000);  // a manual move: claims the head (lib/presets Arbiter)

// Presets + patrol. Each takes the lock and saves to NVS.
void presetsJson(JsonObject out);
bool presetAction(JsonObjectConst in, std::string& error);  // {action: save|go|delete, name, pan?, tilt?}
bool applyPatrolJson(JsonObjectConst in, std::string& error);  // {enabled, presets: [names], dwellS}

}  // namespace lynx
