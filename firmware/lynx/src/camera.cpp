// SPDX-License-Identifier: MIT
#include "camera.h"

#include <cstring>

namespace lynx {

// XIAO ESP32-S3 Sense camera connector (Seeed / arduino-esp32 CAMERA_MODEL_XIAO_ESP32S3)
static const int kPwdn = -1, kReset = -1, kXclk = 10, kSiod = 40, kSioc = 39;
static const int kY9 = 48, kY8 = 11, kY7 = 12, kY6 = 14, kY5 = 16, kY4 = 18, kY3 = 17, kY2 = 15;
static const int kVsync = 38, kHref = 47, kPclk = 13;

static bool ready = false;

struct Size {
  const char* name;
  framesize_t fs;
};
static const Size kSizes[] = {{"QVGA", FRAMESIZE_QVGA}, {"VGA", FRAMESIZE_VGA}, {"SVGA", FRAMESIZE_SVGA},
                              {"XGA", FRAMESIZE_XGA},   {"HD", FRAMESIZE_HD},   {"SXGA", FRAMESIZE_SXGA},
                              {"UXGA", FRAMESIZE_UXGA}};

int framesizeFromName(const char* name) {
  if (!name) return -1;
  for (const Size& s : kSizes)
    if (!strcasecmp(name, s.name)) return s.fs;
  return -1;
}

const char* framesizeName(int fs) {
  for (const Size& s : kSizes)
    if (s.fs == fs) return s.name;
  return "?";
}

bool cameraBegin(const CameraSettings& s, String& error) {
  if (!psramFound()) {
    error = "no PSRAM: the camera needs it (check the board/psram build flags)";
    return false;
  }
  camera_config_t c = {};
  c.pin_pwdn = kPwdn;
  c.pin_reset = kReset;
  c.pin_xclk = kXclk;
  c.pin_sccb_sda = kSiod;
  c.pin_sccb_scl = kSioc;
  c.pin_d7 = kY9;
  c.pin_d6 = kY8;
  c.pin_d5 = kY7;
  c.pin_d4 = kY6;
  c.pin_d3 = kY5;
  c.pin_d2 = kY4;
  c.pin_d1 = kY3;
  c.pin_d0 = kY2;
  c.pin_vsync = kVsync;
  c.pin_href = kHref;
  c.pin_pclk = kPclk;
  c.xclk_freq_hz = 20000000;
  // the servos own LEDC timers 0-1 (ESP32Servo); keep the camera clock well clear of them
  c.ledc_timer = LEDC_TIMER_3;
  c.ledc_channel = LEDC_CHANNEL_7;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_UXGA;  // size the buffers for the largest mode, then drop to the chosen one
  c.jpeg_quality = s.quality;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    error = String("esp_camera_init failed: 0x") + String(err, HEX);
    return false;
  }
  ready = true;
  cameraApply(s);
  return true;
}

bool cameraReady() { return ready; }

void cameraApply(const CameraSettings& s) {
  if (!ready) return;
  sensor_t* sn = esp_camera_sensor_get();
  if (!sn) return;
  sn->set_framesize(sn, (framesize_t)s.framesize);
  sn->set_quality(sn, s.quality);
  sn->set_vflip(sn, s.vflip);
  sn->set_hmirror(sn, s.hmirror);
  sn->set_brightness(sn, s.brightness);
}

camera_fb_t* cameraGrab() { return ready ? esp_camera_fb_get() : nullptr; }

void cameraRelease(camera_fb_t* fb) {
  if (fb) esp_camera_fb_return(fb);
}

bool cameraSnapshot(std::string& jpeg, std::string& error) {
  if (!ready) {
    error = "camera not initialised";
    return false;
  }
  camera_fb_t* fb = cameraGrab();
  if (!fb) {
    error = "no frame from the camera";
    return false;
  }
  jpeg.assign((const char*)fb->buf, fb->len);
  cameraRelease(fb);
  return true;
}

}  // namespace lynx
