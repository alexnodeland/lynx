// SPDX-License-Identifier: MIT
#include "detect_ei.h"

#include "detect.h"

#if __has_include(<lynx_model.h>)
#include <lynx_model.h>  // shim written by `just model-add`, includes the export's <project>_inferencing.h
#define LYNX_HAS_CUSTOM 1
#else
#define LYNX_HAS_CUSTOM 0
#endif

namespace lynx {

#if LYNX_HAS_CUSTOM

static const int kW = EI_CLASSIFIER_INPUT_WIDTH, kH = EI_CLASSIFIER_INPUT_HEIGHT;
static uint8_t rgb[kW * kH * 3];  // the model's input, resized from the detector's frame

bool customAvailable() { return true; }
const char* customModelName() { return EI_CLASSIFIER_PROJECT_NAME; }
int customLabelCount() { return EI_CLASSIFIER_LABEL_COUNT; }
const char* customLabel(int i) { return i >= 0 && i < EI_CLASSIFIER_LABEL_COUNT ? ei_classifier_inferencing_categories[i] : ""; }

// Edge Impulse image models take one float per pixel, packed 0xRRGGBB
static int getData(size_t offset, size_t length, float* out) {
  for (size_t i = 0; i < length; i++) {
    const uint8_t* p = rgb + (offset + i) * 3;
    out[i] = (float)((p[0] << 16) | (p[1] << 8) | p[2]);
  }
  return 0;
}

int customInfer(const uint8_t* rgb565, int w, int h, DetectBox* out, const char** labels, int max) {
  // squash the frame to the model's input (Edge Impulse's default resize mode), nearest neighbour,
  // unpacking the camera's big-endian RGB565 on the way
  for (int y = 0; y < kH; y++) {
    int sy = y * h / kH;
    for (int x = 0; x < kW; x++) {
      int sx = x * w / kW;
      const uint8_t* s = rgb565 + (sy * w + sx) * 2;
      uint16_t v = (s[0] << 8) | s[1];
      uint8_t* d = rgb + (y * kW + x) * 3;
      d[0] = ((v >> 11) & 0x1f) << 3;
      d[1] = ((v >> 5) & 0x3f) << 2;
      d[2] = (v & 0x1f) << 3;
    }
  }
  signal_t sig;
  sig.total_length = kW * kH;
  sig.get_data = &getData;
  ei_impulse_result_t r = {};
  if (run_classifier(&sig, &r, false) != EI_IMPULSE_OK) return -1;
  int n = 0;
#if EI_CLASSIFIER_OBJECT_DETECTION
  for (uint32_t i = 0; i < r.bounding_boxes_count && n < max; i++) {
    const ei_impulse_result_bounding_box_t& b = r.bounding_boxes[i];
    if (b.value <= 0) continue;
    out[n] = {(float)b.x / kW, (float)b.y / kH, (float)b.width / kW, (float)b.height / kH, b.value};
    labels[n++] = b.label;
  }
#else
  // a classifier: report the winning label for the whole frame, if it's confident enough
  int best = 0;
  for (int i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++)
    if (r.classification[i].value > r.classification[best].value) best = i;
  if (max > 0 && r.classification[best].value >= 0.6f) {
    out[n] = {0.f, 0.f, 1.f, 1.f, r.classification[best].value};
    labels[n++] = r.classification[best].label;
  }
#endif
  return n;
}

#else  // no custom model installed

bool customAvailable() { return false; }
const char* customModelName() { return ""; }
int customLabelCount() { return 0; }
const char* customLabel(int) { return ""; }
int customInfer(const uint8_t*, int, int, DetectBox*, const char**, int) { return -1; }

#endif

}  // namespace lynx
