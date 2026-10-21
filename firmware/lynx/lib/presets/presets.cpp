// SPDX-License-Identifier: MIT
#include "presets.h"

#include <cctype>

namespace lynx {

void presetNormalize(const char* in, char out[kPresetNameLen]) {
  int j = 0;
  if (in) {
    while (*in == ' ') in++;
    for (; *in && j < kPresetNameLen - 1; in++) out[j++] = (char)std::tolower((unsigned char)*in);
  }
  while (j > 0 && out[j - 1] == ' ') j--;
  out[j] = 0;
}

bool presetNameValid(const char* name) {
  if (!name || !*name) return false;
  int n = 0;
  for (const char* c = name; *c; c++, n++) {
    if (n >= kPresetNameLen - 1) return false;
    bool ok = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
    if (!ok) return false;
  }
  return true;
}

int Presets::find(const char* name) const {
  char k[kPresetNameLen];
  presetNormalize(name, k);
  for (int i = 0; i < n_; i++)
    if (!strcmp(p_[i].name, k)) return i;
  return -1;
}

bool Presets::save(const char* name, float pan, float tilt) {
  char k[kPresetNameLen];
  presetNormalize(name, k);
  if (!presetNameValid(k)) return false;
  int i = find(k);
  if (i < 0) {
    if (n_ >= kMaxPresets) return false;
    i = n_++;
    strcpy(p_[i].name, k);
  }
  p_[i].pan = pan;
  p_[i].tilt = tilt;
  return true;
}

bool Presets::remove(const char* name) {
  int i = find(name);
  if (i < 0) return false;
  for (int j = i; j < n_ - 1; j++) p_[j] = p_[j + 1];
  n_--;
  return true;
}

void Patrol::start(const int* order, int n, uint32_t dwellMs, uint32_t now) {
  n_ = 0;
  for (int i = 0; i < n && n_ < kMaxPresets; i++) order_[n_++] = order[i];
  dwell_ = dwellMs < 1000 ? 1000 : dwellMs;
  cur_ = -1;
  waiting_ = false;
  arrived_ = now;
  on_ = n_ > 0;
}

Patrol::Step Patrol::tick(uint32_t now, bool allowed, bool idle, const Presets& presets) {
  Step s;
  if (!on_ || n_ == 0) return s;
  if (!allowed) {  // someone else has the head: hold this stop, and start its dwell again afterwards
    arrived_ = now;
    waiting_ = false;
    return s;
  }
  if (!idle) return s;  // still travelling
  if (!waiting_) {      // just arrived (or just allowed back)
    waiting_ = true;
    arrived_ = now;
    if (cur_ >= 0) return s;  // dwell at the current stop first
  }
  if (cur_ >= 0 && (int32_t)(now - arrived_) < (int32_t)dwell_) return s;
  // next stop that still exists
  for (int tries = 0; tries < n_; tries++) {
    cur_ = (cur_ + 1) % n_;
    int p = order_[cur_];
    if (p >= 0 && p < presets.count()) {
      s.move = true;
      s.pan = presets.at(p).pan;
      s.tilt = presets.at(p).tilt;
      waiting_ = false;
      return s;
    }
  }
  on_ = false;  // every stop was deleted
  return s;
}

}  // namespace lynx
