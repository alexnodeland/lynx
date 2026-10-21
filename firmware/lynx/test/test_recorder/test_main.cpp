// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <unity.h>

#include "recorder.h"

using namespace lynx;

void setUp() {}
void tearDown() {}

static int count(Recorder& r, uint32_t from, uint32_t to, bool present, ShotKind k, uint32_t step = 100) {
  int n = 0;
  for (uint32_t t = from; t != to; t += step)
    if (r.tick(t, present) == k) n++;
  return n;
}

static void test_nothing_when_nothing_happens() {
  Recorder r;
  TEST_ASSERT_EQUAL(0, count(r, 0, 600000, false, ShotKind::Event));
  TEST_ASSERT_EQUAL(0, count(r, 600000, 1200000, false, ShotKind::Timelapse));
}

static void test_burst_then_hold() {
  Recorder r;  // burst 3 x 1.5 s, then one every 30 s
  TEST_ASSERT_TRUE(r.tick(10000, true) == ShotKind::Event);  // the moment it appears
  TEST_ASSERT_EQUAL(2, count(r, 10100, 14000, true, ShotKind::Event));
  // then nothing until 30 s after the last burst shot (13 s)
  TEST_ASSERT_EQUAL(0, count(r, 14000, 42900, true, ShotKind::Event));
  TEST_ASSERT_EQUAL(1, count(r, 42900, 44000, true, ShotKind::Event));
}

static void test_reappearing_starts_a_new_burst() {
  Recorder r;
  count(r, 0, 5000, true, ShotKind::Event);
  count(r, 5000, 8000, false, ShotKind::Event);
  TEST_ASSERT_TRUE(r.tick(8000, true) == ShotKind::Event);
}

static void test_detection_off() {
  RecordPolicy p;
  p.onDetect = false;
  Recorder r(p);
  TEST_ASSERT_EQUAL(0, count(r, 0, 60000, true, ShotKind::Event));
}

static void test_timelapse_every_interval_and_first_at_once() {
  RecordPolicy p;
  p.onDetect = false;
  p.timelapseMs = 60000;
  Recorder r(p);
  TEST_ASSERT_TRUE(r.tick(5000, false) == ShotKind::Timelapse);
  TEST_ASSERT_EQUAL(10, count(r, 5100, 605100, false, ShotKind::Timelapse));
}

static void test_event_and_timelapse_coexist() {
  RecordPolicy p;
  p.timelapseMs = 10000;
  Recorder r(p);
  int ev = 0, tl = 0;
  for (uint32_t t = 0; t < 60000; t += 100) {
    ShotKind k = r.tick(t, t >= 20000 && t < 25000);
    ev += k == ShotKind::Event;
    tl += k == ShotKind::Timelapse;
  }
  TEST_ASSERT_EQUAL(3, ev);
  TEST_ASSERT_TRUE(tl >= 5 && tl <= 7);  // a timelapse frame that coincides with an event shot is folded into it
}

static void test_retention() {
  RecordPolicy p;  // 90 % high water, 80 % low water
  RecFile f[5];
  for (int i = 0; i < 5; i++) {
    snprintf(f[i].path, sizeof f[i].path, "/lynx/20261021/00000%d_event.jpg", i);
    f[i].size = 100;
  }
  TEST_ASSERT_EQUAL(0, retentionCount(f, 5, 850, 1000, p));  // under 90 %: keep everything
  TEST_ASSERT_EQUAL(2, retentionCount(f, 5, 950, 1000, p));  // 950 -> 750: two oldest go
  TEST_ASSERT_EQUAL(5, retentionCount(f, 5, 2000, 1000, p));  // can't get there: delete all lynx's own
  TEST_ASSERT_EQUAL(0, retentionCount(f, 5, 950, 0, p));
}

static void test_paths() {
  TEST_ASSERT_TRUE(recPathSafe("/lynx"));
  TEST_ASSERT_TRUE(recPathSafe("/lynx/20261021"));
  TEST_ASSERT_TRUE(recPathSafe("/lynx/20261021/101500_event.jpg"));
  TEST_ASSERT_FALSE(recPathSafe("/lynx/../etc"));
  TEST_ASSERT_FALSE(recPathSafe("/lynxx/a.jpg"));
  TEST_ASSERT_FALSE(recPathSafe("/other/a.jpg"));
  TEST_ASSERT_FALSE(recPathSafe("/lynx/20261021/notes.txt"));
  TEST_ASSERT_FALSE(recPathSafe("/lynx//x.jpg"));
  TEST_ASSERT_FALSE(recPathSafe("/lynx/a\\b.jpg"));
  TEST_ASSERT_FALSE(recPathSafe(nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_when_nothing_happens);
  RUN_TEST(test_burst_then_hold);
  RUN_TEST(test_reappearing_starts_a_new_burst);
  RUN_TEST(test_detection_off);
  RUN_TEST(test_timelapse_every_interval_and_first_at_once);
  RUN_TEST(test_event_and_timelapse_coexist);
  RUN_TEST(test_retention);
  RUN_TEST(test_paths);
  return UNITY_END();
}
