// SPDX-License-Identifier: MIT
// A custom model from Edge Impulse, if one is installed (`just model-add export.zip` puts it in
// lib/lynx_model/). Object-detection (FOMO) exports give boxes; image-classification exports give one
// whole-frame label. Without a model, customAvailable() is false and nothing else here is used.
#pragma once
#include <cstddef>
#include <cstdint>

namespace lynx {

struct DetectBox;

bool customAvailable();
const char* customModelName();  // the Edge Impulse project's name, "" without a model
int customLabelCount();
const char* customLabel(int i);
// Run on an RGB565 image (camera byte order). Fills up to `max` boxes; returns the count, or -1 on error.
int customInfer(const uint8_t* rgb565, int w, int h, DetectBox* out, const char** labels, int max);

}  // namespace lynx
