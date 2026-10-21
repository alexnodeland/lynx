// SPDX-License-Identifier: MIT
// What lynx records to its microSD card, and what it deletes when the card fills. Pure logic, tested
// natively; src/sd.cpp carries it out.
//
//   On detection: when something appears, a burst of `burst` shots `burstGapMs` apart; while it stays,
//   one every `holdGapMs`. Nothing while nothing is there.
//   Timelapse: one shot every `timelapseMs` (0 = off), whatever is in view.
//   Retention: past `highWater` of the card, delete the oldest recordings until under `lowWater`.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lynx {

struct RecordPolicy {
  bool onDetect = true;
  int burst = 3;
  uint32_t burstGapMs = 1500;
  uint32_t holdGapMs = 30000;
  uint32_t timelapseMs = 0;
  float highWater = 0.90f, lowWater = 0.80f;
};

enum class ShotKind { None, Event, Timelapse };

class Recorder {
 public:
  explicit Recorder(RecordPolicy p = RecordPolicy()) : p_(p) {}
  RecordPolicy& policy() { return p_; }
  // Call every few hundred ms with whether something is detected now. Returns what to save, if anything.
  ShotKind tick(uint32_t now, bool present);

 private:
  RecordPolicy p_;
  bool was_ = false, timelapseStarted_ = false;
  int burstLeft_ = 0;
  uint32_t lastEvent_ = 0, lastTimelapse_ = 0;
};

struct RecFile {
  char path[48];
  uint64_t size;
};

// Which files to delete so `used` falls under lowWater of `total`, oldest first. `files` must be sorted
// oldest first (lynx's names, /lynx/YYYYMMDD/HHMMSS_kind.jpg, sort that way). Returns how many of the
// first entries to delete; 0 when the card is under highWater.
int retentionCount(const RecFile* files, int n, uint64_t used, uint64_t total, const RecordPolicy& p);

// A path the API may touch: under /lynx/, no "..", no backslashes, a .jpg or a directory.
bool recPathSafe(const char* path);

}  // namespace lynx
