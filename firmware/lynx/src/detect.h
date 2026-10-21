// SPDX-License-Identifier: MIT
// On-device detection with ESP-DL (bundled with arduino-esp32 for the ESP32-S3): human faces
// (two-stage MSR01 + MNP01) or cat faces (MN03). Runs in its own task on a scaled-down copy of
// the camera's JPEG, so the stream and snapshots are untouched. Detection only: nothing is
// recognised, stored or sent anywhere unless asked for over the API.
#pragma once
#include <Arduino.h>

namespace lynx {

enum DetectModel { DETECT_FACE = 0, DETECT_CAT = 1 };
// How small a face can be. near: faces from ~30 % of the frame wide (arm's length, fastest);
// far: down to ~12 %, across a room, at a lower frame rate.
enum DetectRange { RANGE_NEAR = 0, RANGE_FAR = 1 };

struct DetectBox {
  float x, y, w, h;  // normalised to the frame, 0..1, origin top-left
  float score;
};

struct Detections {
  uint32_t seq = 0;   // increments with every processed frame
  uint32_t at = 0;    // millis() of that frame
  int n = 0;
  DetectBox box[8];
  float fps = 0;      // processed frames per second, smoothed
  int inW = 0, inH = 0;  // the scaled image the model saw
  int model = DETECT_FACE;
};

void detectBegin();
void detectSet(bool enabled, int model, int range = RANGE_FAR);  // also persisted by the caller
int detectRange();
const char* detectRangeName(int range);  // "near" | "far"
int detectRangeFromName(const char* name);
bool detectEnabled();
int detectModel();
const char* detectModelName(int model);  // "face" | "cat"
int detectModelFromName(const char* name);  // -1 if unknown
Detections detectLatest();
uint32_t detectLastSeenMs();  // millis() when something was last detected, 0 = never

// Run the current model on a JPEG you supply instead of the next camera frame (testing, tuning).
// Blocks until the detector task has run it (~0.3 s); false if detection is off, busy, or the image can't be decoded.
bool detectImage(const uint8_t* jpeg, size_t len, Detections& out, uint32_t timeoutMs = 5000);
// Diagnosis for the last detectImage: the decoded frame the model saw, as JPEG (caller frees with free()),
// and how many first-stage face candidates there were.
void detectDebugTake(uint8_t** jpeg, size_t* len, int* stage1);
void detectDebugWantImage(bool on);

}  // namespace lynx
