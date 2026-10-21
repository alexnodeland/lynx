// SPDX-License-Identifier: MIT
#include <unity.h>

#include "netplan.h"

using namespace lynx;

void setUp() {}
void tearDown() {}

static const NetPolicy kP{180000, 20000};

// run tick() every 10 ms over [from, to), returning the first non-None action and when it happened
struct Seen {
  NetAction a = NetAction::None;
  uint32_t at = 0;
};
static Seen run(NetPlan& n, uint32_t from, uint32_t to, bool up) {
  for (uint32_t t = from; t != to; t += 10) {
    NetAction a = n.tick(t, up);
    if (a != NetAction::None) return {a, t};
  }
  return {};
}

static void test_fresh_setup_closes_after_linger() {
  NetPlan n(kP);
  TEST_ASSERT_EQUAL(NetAction::OpenAp, n.begin(0, false));
  TEST_ASSERT_TRUE(n.apOpen());
  TEST_ASSERT_EQUAL(NetAction::None, run(n, 0, 600000, false).a);  // waits for setup forever
  n.joining(600000);
  TEST_ASSERT_EQUAL(NetAction::None, run(n, 600000, 605000, false).a);
  Seen s = run(n, 605000, 1000000, true);  // joined at 605 s
  TEST_ASSERT_EQUAL(NetAction::CloseAp, s.a);
  TEST_ASSERT_EQUAL_UINT32(605000 + 180000, s.at);
  TEST_ASSERT_FALSE(n.apOpen());
  TEST_ASSERT_EQUAL(JoinState::Connected, n.join());
}

// the bring-up bug: the first attempt timed out (wrong password, slow DHCP) before the join succeeded
static void test_failed_attempt_then_success_still_closes() {
  NetPlan n(kP);
  n.begin(0, false);
  n.joining(1000);
  TEST_ASSERT_EQUAL(NetAction::None, run(n, 1000, 30000, false).a);  // already open: no second OpenAp
  TEST_ASSERT_EQUAL(JoinState::Failed, n.join());
  Seen s = run(n, 30000, 400000, true);
  TEST_ASSERT_EQUAL(NetAction::CloseAp, s.a);
  TEST_ASSERT_EQUAL_UINT32(30000 + 180000, s.at);
}

static void test_saved_network_boot_never_opens_when_reachable() {
  NetPlan n(kP);
  TEST_ASSERT_EQUAL(NetAction::None, n.begin(0, true));
  TEST_ASSERT_EQUAL(NetAction::None, run(n, 0, 3000, false).a);
  TEST_ASSERT_EQUAL(NetAction::None, run(n, 3000, 900000, true).a);
  TEST_ASSERT_FALSE(n.apOpen());
}

static void test_unreachable_network_opens_then_closes_when_back() {
  NetPlan n(kP);
  n.begin(0, true);
  Seen s = run(n, 0, 60000, false);
  TEST_ASSERT_EQUAL(NetAction::OpenAp, s.a);
  TEST_ASSERT_EQUAL_UINT32(20000, s.at);
  s = run(n, 60000, 400000, true);  // the router came back at 60 s
  TEST_ASSERT_EQUAL(NetAction::CloseAp, s.a);
  TEST_ASSERT_EQUAL_UINT32(60000 + 180000, s.at);
}

static void test_link_drop_restarts_linger_and_can_reopen() {
  NetPlan n(kP);
  n.begin(0, true);
  run(n, 0, 100000, true);
  Seen s = run(n, 100000, 200000, false);  // drops at 100 s and stays down
  TEST_ASSERT_EQUAL(NetAction::OpenAp, s.a);
  TEST_ASSERT_EQUAL_UINT32(120000, s.at);
}

static void test_manual_window_outlasts_linger() {
  NetPlan n(kP);
  n.begin(0, true);
  run(n, 0, 500000, true);  // long connected
  TEST_ASSERT_EQUAL(NetAction::OpenAp, n.openWindow(500000, 600000));
  TEST_ASSERT_EQUAL(NetAction::None, n.openWindow(500100, 600000));  // pressing again doesn't reopen
  Seen s = run(n, 500100, 2000000, true);
  TEST_ASSERT_EQUAL(NetAction::CloseAp, s.a);
  TEST_ASSERT_EQUAL_UINT32(500100 + 600000, s.at);
}

static void test_manual_window_stays_open_while_offline() {
  NetPlan n(kP);
  n.begin(0, true);
  run(n, 0, 1000, true);
  n.openWindow(1000, 60000);
  TEST_ASSERT_EQUAL(NetAction::None, run(n, 1000, 61000, false).a);  // offline: never close
  TEST_ASSERT_TRUE(n.apOpen());
}

static void test_wraps_around_millis() {
  NetPlan n(kP);
  uint32_t t0 = 0xFFFFFFFFu - 50000;
  n.begin(t0, false);
  n.joining(t0);
  Seen s = run(n, t0, t0 + 400000, true);
  TEST_ASSERT_EQUAL(NetAction::CloseAp, s.a);
  TEST_ASSERT_EQUAL_UINT32(t0 + 180000, s.at);
}

static void test_closes_in_reports_remaining() {
  NetPlan n(kP);
  n.begin(0, false);
  n.tick(1000, true);
  TEST_ASSERT_EQUAL_UINT32(180000, n.closesInMs(1000));
  TEST_ASSERT_EQUAL_UINT32(100000, n.closesInMs(81000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_setup_closes_after_linger);
  RUN_TEST(test_failed_attempt_then_success_still_closes);
  RUN_TEST(test_saved_network_boot_never_opens_when_reachable);
  RUN_TEST(test_unreachable_network_opens_then_closes_when_back);
  RUN_TEST(test_link_drop_restarts_linger_and_can_reopen);
  RUN_TEST(test_manual_window_outlasts_linger);
  RUN_TEST(test_manual_window_stays_open_while_offline);
  RUN_TEST(test_wraps_around_millis);
  RUN_TEST(test_closes_in_reports_remaining);
  return UNITY_END();
}
