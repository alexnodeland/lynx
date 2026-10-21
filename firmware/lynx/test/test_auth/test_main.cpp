// SPDX-License-Identifier: MIT
#include <unity.h>

#include "auth.h"

using namespace lynx;

void setUp() {}
void tearDown() {}

static const std::string kTok = "0123456789abcdef0123456789abcdef";

static void test_token_from_bytes_is_32_hex() {
  uint8_t b[16];
  for (int i = 0; i < 16; i++) b[i] = (uint8_t)(i * 17);
  std::string t = tokenFromBytes(b);
  TEST_ASSERT_EQUAL(32, t.size());
  TEST_ASSERT_EQUAL_STRING("00112233445566778899aabbccddeeff", t.c_str());
}

static void test_token_equals() {
  TEST_ASSERT_TRUE(tokenEquals(kTok, kTok));
  TEST_ASSERT_FALSE(tokenEquals(kTok.substr(0, 31), kTok));
  TEST_ASSERT_FALSE(tokenEquals(kTok + "0", kTok));
  TEST_ASSERT_FALSE(tokenEquals("", kTok));
  TEST_ASSERT_FALSE(tokenEquals("", ""));  // an unset token never matches
  std::string other = kTok;
  other[31] = '0';
  TEST_ASSERT_FALSE(tokenEquals(other, kTok));
}

static void test_bearer_and_query() {
  TEST_ASSERT_TRUE(authorized("Bearer " + kTok, "", kTok));
  TEST_ASSERT_TRUE(authorized("bearer  " + kTok + " ", "", kTok));
  TEST_ASSERT_FALSE(authorized("Basic " + kTok, "", kTok));
  TEST_ASSERT_FALSE(authorized("Bearer nope", "", kTok));
  TEST_ASSERT_TRUE(authorized("", kTok, kTok));
  TEST_ASSERT_FALSE(authorized("", "", kTok));
  TEST_ASSERT_FALSE(authorized("Bearer ", "", ""));
}

static void test_origin_guard() {
  const std::string mdns = "lynx-1a2b.local", ip = "192.168.1.9";
  TEST_ASSERT_TRUE(originAllowed("", mdns, ip));  // curl, MCP clients: no Origin
  TEST_ASSERT_TRUE(originAllowed("http://lynx-1a2b.local", mdns, ip));
  TEST_ASSERT_TRUE(originAllowed("http://192.168.1.9:80", mdns, ip));
  TEST_ASSERT_TRUE(originAllowed("http://LYNX-1A2B.local", mdns, ip));
  // DNS rebinding: a page on a domain that resolves to the device
  TEST_ASSERT_FALSE(originAllowed("http://evil.example", mdns, ip));
  TEST_ASSERT_FALSE(originAllowed("http://lynx-1a2b.local.evil.example", mdns, ip));
  TEST_ASSERT_FALSE(originAllowed("null", mdns, ip));
  TEST_ASSERT_FALSE(originAllowed("http://", mdns, ""));
}

static void test_query_value() {
  TEST_ASSERT_EQUAL_STRING("abc", queryValue("token=abc", "token").c_str());
  TEST_ASSERT_EQUAL_STRING("abc", queryValue("fps=5&token=abc", "token").c_str());
  TEST_ASSERT_EQUAL_STRING("", queryValue("tokens=abc", "token").c_str());
  TEST_ASSERT_EQUAL_STRING("", queryValue("", "token").c_str());
}


void test_throttle_free_tries_then_backoff() {
  lynx::AuthThrottle t;
  const uint32_t ip = 0x0a000001;
  for (uint32_t i = 0; i < lynx::AuthThrottle::kFree; i++) {
    TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(ip, 1000));
    t.failure(ip, 1000);
  }
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(ip, 1000));  // five wrong tokens: still no lockout
  t.failure(ip, 1000);                                  // sixth: 1 s
  TEST_ASSERT_EQUAL_UINT32(1000, t.retryAfter(ip, 1000));
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(ip, 2000));
  t.failure(ip, 2000);  // seventh: 2 s
  TEST_ASSERT_EQUAL_UINT32(2000, t.retryAfter(ip, 2000));
  t.failure(ip, 4000);  // eighth: 4 s
  TEST_ASSERT_EQUAL_UINT32(4000, t.retryAfter(ip, 4000));
}

void test_throttle_success_clears_and_ips_independent() {
  lynx::AuthThrottle t;
  for (int i = 0; i < 8; i++) t.failure(1, 0);
  TEST_ASSERT_TRUE(t.retryAfter(1, 0) > 0);
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(2, 0));  // another address is unaffected
  t.success(1);
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(1, 0));
  TEST_ASSERT_EQUAL_UINT32(0, t.failures(1));
}

void test_throttle_caps_forgets_and_evicts() {
  lynx::AuthThrottle t;
  uint32_t now = 0xfffff000;  // straddles the millis() wrap
  for (int i = 0; i < 40; i++) t.failure(7, now);
  TEST_ASSERT_EQUAL_UINT32(lynx::AuthThrottle::kMaxMs, t.retryAfter(7, now));
  // quiet for 10 minutes: forgotten
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfter(7, now + lynx::AuthThrottle::kForgetMs));
  t.failure(7, now + lynx::AuthThrottle::kForgetMs);
  TEST_ASSERT_EQUAL_UINT32(1, t.failures(7));
  // a full table forgets the address seen longest ago
  lynx::AuthThrottle u;
  for (uint32_t ip = 1; ip <= lynx::AuthThrottle::kSlots; ip++) u.failure(ip, ip * 10);
  u.failure(99, 1000);
  TEST_ASSERT_EQUAL_UINT32(0, u.failures(1));
  TEST_ASSERT_EQUAL_UINT32(1, u.failures(2));
  TEST_ASSERT_EQUAL_UINT32(1, u.failures(99));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_token_from_bytes_is_32_hex);
  RUN_TEST(test_token_equals);
  RUN_TEST(test_bearer_and_query);
  RUN_TEST(test_origin_guard);
  RUN_TEST(test_query_value);
  RUN_TEST(test_throttle_free_tries_then_backoff);
  RUN_TEST(test_throttle_success_clears_and_ips_independent);
  RUN_TEST(test_throttle_caps_forgets_and_evicts);
  return UNITY_END();
}
