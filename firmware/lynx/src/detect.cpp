// SPDX-License-Identifier: MIT
#include "detect.h"

#include <esp_camera.h>
#include <freertos/semphr.h>
#include <img_converters.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "camera.h"
#include "ota.h"
#include "detect_ei.h"
#include "cat_face_detect_mn03.hpp"
#include "human_face_detect_mnp01.hpp"
#include "human_face_detect_msr01.hpp"

namespace lynx {

static const int kMaxW = 400, kMaxH = 300;
// jpg2rgb565 writes little-endian pixels; ESP-DL (like fmt2jpg) expects the camera's own byte order
static const bool kSwap565 = true;  // the scaled image never exceeds this (UXGA at 1/4)
static std::atomic<bool> enabled{false};
static std::atomic<int> model{DETECT_FACE};
static std::atomic<int> range{RANGE_FAR};
// first-stage input scale per range: the model sees the frame shrunk by this before looking for faces
static const float kRangeScale[2] = {0.2f, 0.45f};
static SemaphoreHandle_t lock;
static Detections latest;
static std::atomic<uint32_t> lastSeen{0};
static uint8_t* rgb = nullptr;  // RGB565, in PSRAM
// a supplied image waiting for the task (detectImage), and the task's answer
static const uint8_t* testJpeg = nullptr;
static size_t testLen = 0, testW = 0, testH = 0;
static SemaphoreHandle_t testDone, testBusy;
static Detections testResult;
static bool testOk = false;
static bool testWantImage = false;
static uint8_t* testImg = nullptr;
static size_t testImgLen = 0;
static int testStage1 = 0;  // candidates from the first stage, for diagnosis

const char* detectModelName(int m) { return m == DETECT_CAT ? "cat" : (m == DETECT_CUSTOM ? "custom" : "face"); }
bool detectCustomAvailable() { return customAvailable(); }
const char* detectCustomName() { return customModelName(); }
int detectModelFromName(const char* n) {
  if (!n) return -1;
  if (!strcmp(n, "face")) return DETECT_FACE;
  if (!strcmp(n, "cat")) return DETECT_CAT;
  if (!strcmp(n, "custom")) return customAvailable() ? DETECT_CUSTOM : -1;
  return -1;
}

void detectSet(bool on, int m, int r) {
  model = (m == DETECT_CAT || (m == DETECT_CUSTOM && customAvailable())) ? m : DETECT_FACE;
  range = r == RANGE_NEAR ? RANGE_NEAR : RANGE_FAR;
  enabled = on;
}
int detectRange() { return range; }
const char* detectRangeName(int r) { return r == RANGE_NEAR ? "near" : "far"; }
int detectRangeFromName(const char* n) {
  if (!n) return -1;
  if (!strcmp(n, "near")) return RANGE_NEAR;
  if (!strcmp(n, "far")) return RANGE_FAR;
  return -1;
}
bool detectEnabled() { return enabled; }
int detectModel() { return model; }
uint32_t detectLastSeenMs() { return lastSeen; }

Detections detectLatest() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Detections d = latest;
  xSemaphoreGive(lock);
  return d;
}

// pick the JPEG decoder's scale so the image is at most kMaxW wide
static jpg_scale_t scaleFor(int w, int& shift) {
  shift = 0;
  while ((w >> shift) > kMaxW && shift < 3) shift++;
  return (jpg_scale_t)shift;
}

static void task(void*) {
  // thresholds as in Espressif's CameraWebServer example
  // thresholds as in Espressif's CameraWebServer example; the first stage's input scale follows `range`
  int built = -1;
  HumanFaceDetectMSR01* face1 = nullptr;
  CatFaceDetectMN03* cat = nullptr;
  HumanFaceDetectMNP01 face2(0.5F, 0.3F, 5);
  uint32_t lastT = 0;
  float fps = 0;
  for (;;) {
    if (!enabled || !cameraReady() || otaInProgress()) {
      if (testJpeg) {  // switched off with a supplied image waiting: answer, so the caller never waits forever
        testOk = false;
        testJpeg = nullptr;
        xSemaphoreGive(testDone);
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    bool isTest = testJpeg != nullptr;
    int shift, w, h;
    bool ok;
    uint32_t at = millis();
    if (isTest) {
      jpg_scale_t sc = scaleFor(testW, shift);
      w = testW >> shift;
      h = testH >> shift;
      ok = w <= kMaxW && h <= kMaxH && jpg2rgb565(testJpeg, testLen, rgb, sc);
    } else {
      camera_fb_t* fb = cameraGrab();
      if (!fb) {
        vTaskDelay(pdMS_TO_TICKS(30));
        continue;
      }
      jpg_scale_t sc = scaleFor(fb->width, shift);
      w = fb->width >> shift;
      h = fb->height >> shift;
      ok = fb->format == PIXFORMAT_JPEG && w <= kMaxW && h <= kMaxH && jpg2rgb565(fb->buf, fb->len, rgb, sc);
      cameraRelease(fb);  // give the frame back before the slow part
    }
    if (!ok && isTest) {
      testOk = false;
      testJpeg = nullptr;
      xSemaphoreGive(testDone);
      continue;
    }
    if (!ok) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (kSwap565) {
      uint16_t* px = (uint16_t*)rgb;
      for (int i = 0; i < w * h; i++) px[i] = (px[i] >> 8) | (px[i] << 8);
    }
    std::vector<int> shape = {h, w, 3};
    if (isTest && testWantImage) {  // diagnosis: what the model sees, re-encoded
      free(testImg);
      testImg = nullptr;
      testImgLen = 0;
      fmt2jpg(rgb, w * h * 2, w, h, PIXFORMAT_RGB565, 85, &testImg, &testImgLen);
    }
    int m = model;
    if (built != range) {
      built = range;
      delete face1;
      delete cat;
      face1 = new HumanFaceDetectMSR01(0.1F, 0.5F, 10, kRangeScale[built]);
      cat = new CatFaceDetectMN03(0.4F, 0.3F, 10, kRangeScale[built] * 1.5f);
    }
    Detections d;
    d.at = at;
    d.inW = w;
    d.inH = h;
    d.model = m;
    std::list<dl::detect::result_t>* res = nullptr;
    if (m == DETECT_CUSTOM) {
      int n = customInfer(rgb, w, h, d.box, d.label, 8);
      d.n = n < 0 ? 0 : n;
    } else if (m == DETECT_CAT) {
      res = &cat->infer((uint16_t*)rgb, shape);
    } else {
      std::list<dl::detect::result_t>& cand = face1->infer((uint16_t*)rgb, shape);
      if (isTest) testStage1 = cand.size();
      res = &face2.infer((uint16_t*)rgb, shape, cand);  // second stage refines and confirms
    }
    const char* name = detectModelName(m);
    for (int i = 0; i < d.n; i++)
      if (!d.label[i]) d.label[i] = name;
    if (res) for (auto& r : *res) {
      if (d.n >= 8 || r.box.size() < 4) break;
      float x1 = constrain(r.box[0], 0, w), y1 = constrain(r.box[1], 0, h);
      float x2 = constrain(r.box[2], 0, w), y2 = constrain(r.box[3], 0, h);
      if (x2 <= x1 || y2 <= y1) continue;
      // the face model scores 0..1; the cat model's come out raw, so squash those to 0..1 too
      float sc = (r.score < 0.f || r.score > 1.f) ? 1.f / (1.f + expf(-r.score)) : r.score;
      d.label[d.n] = name;
      d.box[d.n++] = {x1 / w, y1 / h, (x2 - x1) / w, (y2 - y1) / h, sc};
    }
    if (isTest) {  // answer the caller; don't touch the live results or the follow state
      testResult = d;
      testOk = true;
      testJpeg = nullptr;
      xSemaphoreGive(testDone);
      continue;
    }
    if (lastT) fps = fps * 0.8f + 0.2f * (1000.f / max<uint32_t>(1, at - lastT));
    lastT = at;
    d.fps = fps;
    if (d.n) lastSeen = at;
    xSemaphoreTake(lock, portMAX_DELAY);
    d.seq = latest.seq + 1;
    latest = d;
    xSemaphoreGive(lock);
    vTaskDelay(1);  // let the loop and Wi-Fi breathe
  }
}

// JPEG size from its SOF marker (the decoder needs it to choose a scale)
static bool jpegSize(const uint8_t* p, size_t n, size_t& w, size_t& h) {
  for (size_t i = 2; i + 9 < n;) {
    if (p[i] != 0xFF) return false;
    uint8_t m = p[i + 1];
    size_t len = (p[i + 2] << 8) | p[i + 3];
    if (m >= 0xC0 && m <= 0xC3) {
      h = (p[i + 5] << 8) | p[i + 6];
      w = (p[i + 7] << 8) | p[i + 8];
      return w && h;
    }
    i += 2 + len;
  }
  return false;
}

bool detectImage(const uint8_t* jpeg, size_t len, Detections& out, uint32_t timeoutMs) {
  if (!enabled || !rgb) return false;
  size_t w, h;
  if (!jpegSize(jpeg, len, w, h)) return false;
  if (xSemaphoreTake(testBusy, 0) != pdTRUE) return false;
  xSemaphoreTake(testDone, 0);  // clear a stale give
  testW = w;
  testH = h;
  testLen = len;
  testJpeg = jpeg;  // the task picks it up on its next pass
  // the task always answers (even when switched off meanwhile), so wait for it: giving up early would
  // free the caller's buffer while the task may still be decoding it
  (void)timeoutMs;
  xSemaphoreTake(testDone, portMAX_DELAY);
  bool ok = testOk;
  if (ok) out = testResult;
  xSemaphoreGive(testBusy);
  return ok;
}

bool detectAnnotatedSnapshot(std::string& out, std::string& error) {
  std::string jpeg;
  if (!cameraSnapshot(jpeg, error)) return false;
  size_t w0, h0;
  if (!jpegSize((const uint8_t*)jpeg.data(), jpeg.size(), w0, h0)) {
    error = "couldn't read the frame";
    return false;
  }
  int shift = 0;
  while ((w0 >> shift) > 800 && shift < 3) shift++;
  int w = w0 >> shift, h = h0 >> shift;
  uint8_t* px = (uint8_t*)ps_malloc((size_t)w * h * 2);
  if (!px) {
    error = "out of memory";
    return false;
  }
  if (!jpg2rgb565((const uint8_t*)jpeg.data(), jpeg.size(), px, (jpg_scale_t)shift)) {
    free(px);
    error = "couldn't decode the frame";
    return false;
  }
  uint16_t* p16 = (uint16_t*)px;
  for (int i = 0; i < w * h; i++) p16[i] = (p16[i] >> 8) | (p16[i] << 8);  // to the camera's byte order (fmt2jpg's)
  Detections d = detectLatest();
  bool fresh = detectEnabled() && d.at && millis() - d.at < 2000;
  const uint8_t hi = 0x07, lo = 0xE0;  // green, RGB565, big-endian
  int t = max(2, w / 200);
  for (int i = 0; fresh && i < d.n; i++) {
    int x0 = d.box[i].x * w, y0 = d.box[i].y * h, x1 = (d.box[i].x + d.box[i].w) * w, y1 = (d.box[i].y + d.box[i].h) * h;
    x0 = constrain(x0, 0, w - 1); x1 = constrain(x1, 0, w - 1); y0 = constrain(y0, 0, h - 1); y1 = constrain(y1, 0, h - 1);
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        bool edge = x - x0 < t || x1 - x < t || y - y0 < t || y1 - y < t;
        if (!edge) continue;
        uint8_t* q = px + (y * w + x) * 2;
        q[0] = hi;
        q[1] = lo;
      }
  }
  uint8_t* enc = nullptr;
  size_t encLen = 0;
  bool ok = fmt2jpg(px, (size_t)w * h * 2, w, h, PIXFORMAT_RGB565, 80, &enc, &encLen);
  free(px);
  if (!ok) {
    error = "couldn't encode the annotated frame";
    return false;
  }
  out.assign((const char*)enc, encLen);
  free(enc);
  return true;
}

void detectDebugWantImage(bool on) { testWantImage = on; }
void detectDebugTake(uint8_t** jpeg, size_t* len, int* stage1) {
  *jpeg = testImg;
  *len = testImgLen;
  *stage1 = testStage1;
  testImg = nullptr;
  testImgLen = 0;
}

void detectBegin() {
  lock = xSemaphoreCreateMutex();
  testDone = xSemaphoreCreateBinary();
  testBusy = xSemaphoreCreateMutex();
  rgb = (uint8_t*)ps_malloc(kMaxW * kMaxH * 2);
  if (!rgb) {
    Serial.println("detect: no PSRAM for the frame copy; detection unavailable");
    return;
  }
  // core 1 with the Arduino loop, low priority: Wi-Fi (core 0) and the HTTP servers always win
  xTaskCreatePinnedToCore(task, "detect", 12288, nullptr, 1, nullptr, 1);
}

}  // namespace lynx
