// SPDX-License-Identifier: MIT
// OV2640/OV3660 on the XIAO ESP32-S3 Sense, via esp32-camera.
#pragma once
#include <Arduino.h>
#include <esp_camera.h>

#include <string>

#include "settings.h"

namespace lynx {

bool cameraBegin(const CameraSettings& s, String& error);
bool cameraReady();
void cameraApply(const CameraSettings& s);

// A frame from the driver; release it promptly (the stream and snapshots share two buffers).
camera_fb_t* cameraGrab();
void cameraRelease(camera_fb_t* fb);

// JPEG copy for callers that outlive the frame buffer (MCP snapshot).
bool cameraSnapshot(std::string& jpeg, std::string& error);

// "VGA" <-> FRAMESIZE_VGA; -1 for names we don't offer.
int framesizeFromName(const char* name);
const char* framesizeName(int fs);

}  // namespace lynx
