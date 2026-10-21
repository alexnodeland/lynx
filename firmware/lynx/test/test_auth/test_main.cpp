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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_token_from_bytes_is_32_hex);
  RUN_TEST(test_token_equals);
  RUN_TEST(test_bearer_and_query);
  RUN_TEST(test_origin_guard);
  RUN_TEST(test_query_value);
  return UNITY_END();
}
