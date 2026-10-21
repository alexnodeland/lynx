// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <unity.h>

#include "presets.h"

using namespace lynx;

void setUp() {}
void tearDown() {}

static void test_names() {
  TEST_ASSERT_TRUE(presetNameValid("door"));
  TEST_ASSERT_TRUE(presetNameValid("desk-2_left"));
  TEST_ASSERT_FALSE(presetNameValid(""));
  TEST_ASSERT_FALSE(presetNameValid("Door"));  // must be normalised first
  TEST_ASSERT_FALSE(presetNameValid("front door"));
  TEST_ASSERT_FALSE(presetNameValid("abcdefghijklmnop"));  // 16: too long
  char n[kPresetNameLen];
  presetNormalize("  Front-Door ", n);
  TEST_ASSERT_EQUAL_STRING("front-door", n);
}

static void test_save_replace_remove() {
  Presets p;
  TEST_ASSERT_TRUE(p.save("Door", 60, 50));
  TEST_ASSERT_TRUE(p.save("desk", -30, 40));
  TEST_ASSERT_EQUAL(2, p.count());
  TEST_ASSERT_TRUE(p.save("door", 65, 52));  // replace, case-insensitively
  TEST_ASSERT_EQUAL(2, p.count());
  TEST_ASSERT_EQUAL_FLOAT(65, p.at(p.find("DOOR")).pan);
  TEST_ASSERT_FALSE(p.save("bad name", 0, 0));
  TEST_ASSERT_TRUE(p.remove("door"));
  TEST_ASSERT_FALSE(p.remove("door"));
  TEST_ASSERT_EQUAL(1, p.count());
  TEST_ASSERT_EQUAL_STRING("desk", p.at(0).name);
}

static void test_capacity() {
  Presets p;
  char n[8];
  for (int i = 0; i < kMaxPresets; i++) {
    snprintf(n, sizeof n, "p%d", i);
    TEST_ASSERT_TRUE(p.save(n, i, 0));
  }
  TEST_ASSERT_FALSE(p.save("one-more", 0, 0));
  TEST_ASSERT_TRUE(p.save("p3", 99, 0));  // replacing still works when full
}

// a head that takes `travel` ms to get anywhere
struct Head {
  uint32_t busyUntil = 0;
  float pan = 0, tilt = 45;
  bool idle(uint32_t now) const { return (int32_t)(now - busyUntil) >= 0; }
  void go(uint32_t now, float p, float t) { pan = p; tilt = t; busyUntil = now + 800; }
};

static void test_patrol_tours_and_dwells() {
  Presets p;
  p.save("a", -40, 40);
  p.save("b", 0, 30);
  p.save("c", 40, 40);
  Patrol pat;
  int order[] = {0, 1, 2};
  pat.start(order, 3, 3000, 0);
  Head h;
  float visited[8];
  uint32_t at[8];
  int n = 0;
  for (uint32_t t = 0; t < 20000 && n < 5; t += 50) {
    Patrol::Step s = pat.tick(t, true, h.idle(t), p);
    if (s.move) {
      h.go(t, s.pan, s.tilt);
      visited[n] = s.pan;
      at[n++] = t;
    }
  }
  TEST_ASSERT_EQUAL(5, n);
  TEST_ASSERT_EQUAL_FLOAT(-40, visited[0]);
  TEST_ASSERT_EQUAL_FLOAT(0, visited[1]);
  TEST_ASSERT_EQUAL_FLOAT(40, visited[2]);
  TEST_ASSERT_EQUAL_FLOAT(-40, visited[3]);  // wraps around
  TEST_ASSERT_EQUAL(0, at[0]);
  TEST_ASSERT_TRUE(at[1] - at[0] >= 3000 + 800);  // travel, then the full dwell
}

static void test_patrol_holds_while_blocked() {
  Presets p;
  p.save("a", -40, 40);
  p.save("b", 40, 40);
  Patrol pat;
  int order[] = {0, 1};
  pat.start(order, 2, 2000, 0);
  Head h;
  Patrol::Step s = pat.tick(0, true, true, p);
  TEST_ASSERT_TRUE(s.move);
  h.go(0, s.pan, s.tilt);
  // someone takes over at 1 s for 10 s: patrol must not move, then dwell again before moving on
  for (uint32_t t = 1000; t < 11000; t += 50) TEST_ASSERT_FALSE(pat.tick(t, false, true, p).move);
  TEST_ASSERT_FALSE(pat.tick(11000, true, true, p).move);
  TEST_ASSERT_FALSE(pat.tick(12500, true, true, p).move);
  s = pat.tick(13100, true, true, p);
  TEST_ASSERT_TRUE(s.move);
  TEST_ASSERT_EQUAL_FLOAT(40, s.pan);
}

static void test_patrol_skips_deleted_and_stops_when_empty() {
  Presets p;
  p.save("a", -40, 40);
  Patrol pat;
  int order[] = {5, 0};  // 5 doesn't exist
  pat.start(order, 2, 1000, 0);
  Patrol::Step s = pat.tick(0, true, true, p);
  TEST_ASSERT_TRUE(s.move);
  TEST_ASSERT_EQUAL_FLOAT(-40, s.pan);
  p.clear();
  pat.tick(100, true, true, p);
  pat.tick(2000, true, true, p);
  TEST_ASSERT_FALSE(pat.on());
}

static void test_arbiter_precedence() {
  Arbiter a;
  TEST_ASSERT_TRUE(a.followMay(0));
  TEST_ASSERT_TRUE(a.patrolMay(0, false));
  TEST_ASSERT_FALSE(a.patrolMay(0, true));  // follow with a target beats patrol
  a.manual(1000, 5000);
  TEST_ASSERT_FALSE(a.followMay(3000));  // a person's command beats both
  TEST_ASSERT_FALSE(a.patrolMay(3000, false));
  TEST_ASSERT_TRUE(a.followMay(6000));
  TEST_ASSERT_TRUE(a.patrolMay(6000, false));
}

static void test_arbiter_wraps() {
  Arbiter a;
  a.manual(0xFFFFFF00u, 5000);
  TEST_ASSERT_TRUE(a.manualHeld(100));
  TEST_ASSERT_FALSE(a.manualHeld(6000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names);
  RUN_TEST(test_save_replace_remove);
  RUN_TEST(test_capacity);
  RUN_TEST(test_patrol_tours_and_dwells);
  RUN_TEST(test_patrol_holds_while_blocked);
  RUN_TEST(test_patrol_skips_deleted_and_stops_when_empty);
  RUN_TEST(test_arbiter_precedence);
  RUN_TEST(test_arbiter_wraps);
  return UNITY_END();
}
