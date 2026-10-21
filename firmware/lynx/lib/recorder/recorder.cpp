// SPDX-License-Identifier: MIT
#include "recorder.h"

namespace lynx {

static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

ShotKind Recorder::tick(uint32_t now, bool present) {
  ShotKind k = ShotKind::None;
  if (p_.onDetect) {
    if (present && !was_) {  // something just appeared: start a burst now
      burstLeft_ = p_.burst;
      lastEvent_ = now - p_.burstGapMs;
    }
    if (present) {
      uint32_t gap = burstLeft_ > 0 ? p_.burstGapMs : p_.holdGapMs;
      if (reached(now, lastEvent_ + gap)) {
        k = ShotKind::Event;
        lastEvent_ = now;
        if (burstLeft_ > 0) burstLeft_--;
      }
    } else {
      burstLeft_ = 0;
    }
  }
  was_ = present;
  if (k == ShotKind::None && p_.timelapseMs > 0) {
    if (!timelapseStarted_) {
      timelapseStarted_ = true;
      lastTimelapse_ = now;
      k = ShotKind::Timelapse;  // the first frame straight away
    } else if (reached(now, lastTimelapse_ + p_.timelapseMs)) {
      lastTimelapse_ = now;
      k = ShotKind::Timelapse;
    }
  } else if (p_.timelapseMs == 0) {
    timelapseStarted_ = false;
  }
  return k;
}

int retentionCount(const RecFile* files, int n, uint64_t used, uint64_t total, const RecordPolicy& p) {
  if (total == 0 || (double)used < p.highWater * (double)total) return 0;
  double target = p.lowWater * (double)total;
  int k = 0;
  while (k < n && (double)used > target) used -= files[k++].size;
  return k;
}

bool recPathSafe(const char* path) {
  if (!path || strncmp(path, "/lynx", 5) != 0) return false;
  if (path[5] != 0 && path[5] != '/') return false;
  if (strstr(path, "..") || strchr(path, '\\') || strstr(path, "//")) return false;
  for (const char* c = path; *c; c++)
    if ((unsigned char)*c < 0x20) return false;
  const char* dot = strrchr(path, '.');
  const char* slash = strrchr(path, '/');
  return !dot || dot < slash || !strcmp(dot, ".jpg");
}

}  // namespace lynx
