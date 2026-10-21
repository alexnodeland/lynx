// SPDX-License-Identifier: MIT
// Named positions ("door", "desk"), a patrol that tours them, and the arbiter that decides who may
// move the head when several things want to: a person (manual), follow mode, or the patrol.
// Pure logic, unit-tested natively.
#pragma once
#include <cstdint>
#include <cstring>

namespace lynx {

constexpr int kMaxPresets = 8;
constexpr int kPresetNameLen = 16;  // including the terminator

struct Preset {
  char name[kPresetNameLen] = {};
  float pan = 0, tilt = 0;
};

// Names are 1-15 characters of a-z, 0-9, '-' and '_' (lowercased on the way in), so they work as
// URL parts, Home Assistant options and MCP arguments without escaping.
bool presetNameValid(const char* name);
void presetNormalize(const char* in, char out[kPresetNameLen]);  // lowercase, trimmed, truncated

class Presets {
 public:
  int count() const { return n_; }
  const Preset& at(int i) const { return p_[i]; }
  int find(const char* name) const;  // -1 if absent
  // Add or replace by name. False when the name is invalid or all slots are taken.
  bool save(const char* name, float pan, float tilt);
  bool remove(const char* name);
  void clear() { n_ = 0; }
  // raw access, for persistence
  const Preset* data() const { return p_; }
  void load(const Preset* p, int n) {
    n_ = 0;
    for (int i = 0; i < n && i < kMaxPresets; i++)
      if (presetNameValid(p[i].name)) p_[n_++] = p[i];
  }

 private:
  Preset p_[kMaxPresets];
  int n_ = 0;
};

// Tours presets in order, dwelling at each. It only moves when the head is free (Arbiter) and idle;
// while it's blocked, it waits at its current stop and resumes there.
class Patrol {
 public:
  struct Step {
    bool move = false;
    float pan = 0, tilt = 0;
  };
  // `order` are indices into `presets`; out-of-range ones are skipped.
  void start(const int* order, int n, uint32_t dwellMs, uint32_t now);
  void stop() { on_ = false; }
  bool on() const { return on_; }
  uint32_t dwellMs() const { return dwell_; }
  int length() const { return n_; }
  int stopAt(int i) const { return order_[i]; }
  int current() const { return cur_; }
  // Call every loop. `allowed`: the arbiter lets patrol move now; `idle`: the head isn't moving.
  Step tick(uint32_t now, bool allowed, bool idle, const Presets& presets);

 private:
  int order_[kMaxPresets] = {};
  int n_ = 0, cur_ = -1;
  uint32_t dwell_ = 10000, arrived_ = 0;
  bool on_ = false, waiting_ = false;
};

// Who gets the head: manual commands win for a while after they're given; then follow while it has
// a target; then patrol. Follow without a target yields to patrol, so a patrol keeps going until
// someone appears.
class Arbiter {
 public:
  void manual(uint32_t now, uint32_t holdMs) { until_ = now + holdMs; held_ = true; }
  bool manualHeld(uint32_t now) const { return held_ && (int32_t)(now - until_) < 0; }
  bool followMay(uint32_t now) const { return !manualHeld(now); }
  bool patrolMay(uint32_t now, bool followTracking) const { return !manualHeld(now) && !followTracking; }

 private:
  uint32_t until_ = 0;
  bool held_ = false;
};

}  // namespace lynx
