// SPDX-License-Identifier: MIT
#include <unity.h>

#include <cstring>
#include <vector>

#include "mcp.h"

using namespace lynx;

struct FakeDevice : Device {
  float pan = 0, tilt = 45;
  int32_t lastMs = 0;
  int centers = 0, sweeps = 0;
  bool camOk = true;
  std::string lastFramesize;
  std::string name() override { return "lynx-test"; }
  std::string version() override { return "0.0.1"; }
  void status(JsonObject o) override {
    o["pan"]["target"] = pan;
    o["tilt"]["target"] = tilt;
  }
  uint32_t look(float p, float t, int32_t ms) override {
    pan = p;
    tilt = t;
    lastMs = ms;
    return 640;
  }
  void center() override { centers++; }
  void sweep() override { sweeps++; }
  bool lastAnnotate = false;
  bool snapshot(std::string& jpeg, std::string& err, bool annotate) override {
    lastAnnotate = annotate;
    if (!camOk) {
      err = "camera not ready";
      return false;
    }
    jpeg = std::string("\xff\xd8\xff\xd9", 4);
    return true;
  }
  bool setCamera(JsonObjectConst s, std::string& err) override {
    if (s["quality"].is<int>() && s["quality"].as<int>() < 4) {
      err = "quality must be 4..63";
      return false;
    }
    lastFramesize = s["framesize"] | "";
    return true;
  }
  std::string snapshotUrl() override { return "http://lynx-test.local/snapshot.jpg"; }
  std::string streamUrl() override { return "http://lynx-test.local:81/stream"; }
  bool detectOn = false, followOn = false;
  std::string model = "face";
  void detections(JsonObject o) override {
    o["enabled"] = detectOn;
    o["model"] = model;
    JsonArray a = o["objects"].to<JsonArray>();
    if (!detectOn) return;
    JsonObject b = a.add<JsonObject>();
    b["label"] = model;
    b["score"] = 0.93;
    JsonArray box = b["box"].to<JsonArray>();
    for (float v : {0.7f, 0.2f, 0.2f, 0.25f}) box.add(v);
  }
  bool setDetect(JsonObjectConst s, std::string& err) override {
    if (!s["model"].isNull()) {
      std::string m = s["model"].as<std::string>();
      if (m != "face" && m != "cat") {
        err = "model must be face or cat";
        return false;
      }
      model = m;
    }
    if (!s["enabled"].isNull()) detectOn = s["enabled"].as<bool>();
    return true;
  }
  std::vector<std::string> names;
  bool patrolOn = false;
  void presets(JsonObject o) override {
    JsonArray a = o["presets"].to<JsonArray>();
    for (auto& n : names) {
      JsonObject p = a.add<JsonObject>();
      p["name"] = n;
      p["pan"] = 10;
      p["tilt"] = 40;
    }
    o["patrol"]["enabled"] = patrolOn;
  }
  bool preset(JsonObjectConst s, std::string& err) override {
    std::string a = s["action"] | "", n = s["name"] | "";
    if (a == "save") { names.push_back(n); return true; }
    for (auto it = names.begin(); it != names.end(); ++it)
      if (*it == n) { if (a == "delete") names.erase(it); return true; }
    err = "no preset called '" + n + "'";
    return false;
  }
  bool setPatrol(JsonObjectConst s, std::string&) override {
    patrolOn = s["enabled"].as<bool>();
    return true;
  }
  void recordings(JsonObject o) override { o["mounted"] = false; }
  bool setRecording(JsonObjectConst, std::string& e) override { e = "no microSD card"; return false; }
  bool listRecordings(const char*, JsonArray, std::string& e) override { e = "no microSD card"; return false; }
  bool readRecording(const char*, std::string&, std::string& e) override { e = "no microSD card"; return false; }
  bool setFollow(JsonObjectConst s, std::string&) override {
    followOn = s["enabled"].as<bool>();
    if (followOn) detectOn = true;
    return true;
  }
};

void setUp() {}
void tearDown() {}

static FakeDevice dev;

// body for a 2026-07-28 request
static std::string modern(const char* method, const char* params = "{}", int id = 1) {
  JsonDocument d;
  d["jsonrpc"] = "2.0";
  d["id"] = id;
  d["method"] = method;
  JsonDocument p;
  deserializeJson(p, params);
  d["params"] = p;
  d["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "2026-07-28";
  d["params"]["_meta"]["io.modelcontextprotocol/clientCapabilities"].to<JsonObject>();
  std::string s;
  serializeJson(d, s);
  return s;
}

static McpResponse call(const std::string& body, const char* proto = "", const char* method = "",
                        const char* name = "") {
  McpRequest r;
  r.body = body;
  r.protocolHeader = proto;
  r.methodHeader = method;
  r.nameHeader = name;
  return handleMcp(dev, r);
}

static JsonDocument parse(const McpResponse& r) {
  JsonDocument d;
  deserializeJson(d, r.body);
  return d;
}

static void test_legacy_initialize_negotiates_version() {
  auto r = call(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"c","version":"1"}}})");
  TEST_ASSERT_EQUAL(200, r.status);
  auto d = parse(r);
  TEST_ASSERT_EQUAL_STRING("2025-06-18", d["result"]["protocolVersion"]);
  TEST_ASSERT_EQUAL_STRING("lynx-test", d["result"]["serverInfo"]["name"]);
  TEST_ASSERT_TRUE(d["result"]["capabilities"]["tools"].is<JsonObject>());
  TEST_ASSERT_TRUE(d["result"]["resultType"].isNull());  // legacy results have no resultType

  // an unknown version gets the newest legacy one back
  d = parse(call(R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2024-11-05"}})"));
  TEST_ASSERT_EQUAL_STRING("2025-11-25", d["result"]["protocolVersion"]);
}

static void test_notifications_are_accepted_without_body() {
  auto r = call(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_TRUE(r.body.empty());
}

static void test_modern_discover() {
  auto r = call(modern("server/discover"), "2026-07-28", "server/discover");
  TEST_ASSERT_EQUAL(200, r.status);
  auto d = parse(r);
  TEST_ASSERT_EQUAL_STRING("complete", d["result"]["resultType"]);
  TEST_ASSERT_EQUAL_STRING("2026-07-28", d["result"]["supportedVersions"][0]);
  TEST_ASSERT_EQUAL_STRING("lynx-test", d["result"]["_meta"]["io.modelcontextprotocol/serverInfo"]["name"]);
}

static void test_modern_header_mismatch_and_unsupported() {
  auto r = call(modern("tools/list"), "2025-11-25", "tools/list");
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL(-32020, parse(r)["error"]["code"].as<int>());

  r = call(modern("tools/list"), "2026-07-28", "tools/call");
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL(-32020, parse(r)["error"]["code"].as<int>());

  r = call(modern("tools/call", R"({"name":"status"})"), "2026-07-28", "tools/call", "look");
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL(-32020, parse(r)["error"]["code"].as<int>());

  std::string b = modern("ping");
  b.replace(b.find("2026-07-28"), 10, "2099-01-01");
  r = call(b, "2099-01-01", "ping");
  TEST_ASSERT_EQUAL(400, r.status);
  auto d = parse(r);
  TEST_ASSERT_EQUAL(-32022, d["error"]["code"].as<int>());
  TEST_ASSERT_EQUAL_STRING("2099-01-01", d["error"]["data"]["requested"]);
  TEST_ASSERT_EQUAL_STRING("2026-07-28", d["error"]["data"]["supported"][0]);

  // legacy request with an unknown negotiated version in the header
  r = call(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})", "1900-01-01");
  TEST_ASSERT_EQUAL(400, r.status);
}

static void test_unknown_method() {
  auto r = call(modern("resources/list"), "2026-07-28", "resources/list");
  TEST_ASSERT_EQUAL(404, r.status);
  TEST_ASSERT_EQUAL(-32601, parse(r)["error"]["code"].as<int>());
  r = call(R"({"jsonrpc":"2.0","id":4,"method":"resources/list"})", "2025-11-25");
  TEST_ASSERT_EQUAL(200, r.status);
  TEST_ASSERT_EQUAL(-32601, parse(r)["error"]["code"].as<int>());
}

static void test_bad_json_and_batch() {
  TEST_ASSERT_EQUAL(-32700, parse(call("{nope"))["error"]["code"].as<int>());
  TEST_ASSERT_EQUAL(-32600, parse(call("[]"))["error"]["code"].as<int>());
  TEST_ASSERT_EQUAL(-32600, parse(call(R"({"id":1,"method":"ping"})"))["error"]["code"].as<int>());
}

static void test_tools_list_names() {
  auto d = parse(call(R"({"jsonrpc":"2.0","id":5,"method":"tools/list"})", "2025-11-25"));
  JsonArray t = d["result"]["tools"];
  const char* want[] = {"status", "look", "center", "sweep", "snapshot", "stream_url", "set_camera", "detect", "follow", "preset", "patrol", "recordings"};
  TEST_ASSERT_EQUAL(12, t.size());
  for (int i = 0; i < 12; i++) {
    TEST_ASSERT_EQUAL_STRING(want[i], t[i]["name"]);
    TEST_ASSERT_EQUAL_STRING("object", t[i]["inputSchema"]["type"]);
  }
  TEST_ASSERT_EQUAL(88, t[1]["inputSchema"]["properties"]["pan"]["maximum"].as<int>());
}

static void test_look_center_sweep() {
  auto d = parse(call(modern("tools/call", R"({"name":"look","arguments":{"pan":30,"tilt":10,"ms":500}})"),
                      "2026-07-28", "tools/call", "look"));
  TEST_ASSERT_FALSE(d["result"]["isError"].as<bool>());
  TEST_ASSERT_EQUAL_FLOAT(30, dev.pan);
  TEST_ASSERT_EQUAL_FLOAT(10, dev.tilt);
  TEST_ASSERT_EQUAL(500, dev.lastMs);

  // tilt only: pan keeps its target
  call(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"look","arguments":{"tilt":20}}})");
  TEST_ASSERT_EQUAL_FLOAT(30, dev.pan);
  TEST_ASSERT_EQUAL_FLOAT(20, dev.tilt);
  TEST_ASSERT_EQUAL(-1, dev.lastMs);

  // no angles at all is an invalid-params error
  auto r = call(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"look","arguments":{}}})");
  TEST_ASSERT_EQUAL(-32602, parse(r)["error"]["code"].as<int>());

  int c = dev.centers, s = dev.sweeps;
  call(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"center"}})");
  call(R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"sweep"}})");
  TEST_ASSERT_EQUAL(c + 1, dev.centers);
  TEST_ASSERT_EQUAL(s + 1, dev.sweeps);
}

static void test_snapshot_returns_image_or_tool_error() {
  dev.camOk = true;
  auto d = parse(call(R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"snapshot"}})"));
  TEST_ASSERT_EQUAL_STRING("image", d["result"]["content"][0]["type"]);
  TEST_ASSERT_EQUAL_STRING("image/jpeg", d["result"]["content"][0]["mimeType"]);
  TEST_ASSERT_EQUAL_STRING("/9j/2Q==", d["result"]["content"][0]["data"]);
  TEST_ASSERT_FALSE(dev.lastAnnotate);
  call(R"({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"snapshot","arguments":{"annotate":true}}})");
  TEST_ASSERT_TRUE(dev.lastAnnotate);
  dev.camOk = false;
  d = parse(call(R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"snapshot"}})"));
  TEST_ASSERT_TRUE(d["result"]["isError"].as<bool>());  // a tool failure, not a protocol error
  dev.camOk = true;
}

static void test_stream_url_and_camera() {
  auto d = parse(call(R"({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"stream_url"}})"));
  TEST_ASSERT_EQUAL_STRING("http://lynx-test.local:81/stream", d["result"]["structuredContent"]["stream"]);
  call(R"({"jsonrpc":"2.0","id":13,"method":"tools/call","params":{"name":"set_camera","arguments":{"framesize":"SVGA"}}})");
  TEST_ASSERT_EQUAL_STRING("SVGA", dev.lastFramesize.c_str());
  auto r = call(R"({"jsonrpc":"2.0","id":14,"method":"tools/call","params":{"name":"set_camera","arguments":{"quality":1}}})");
  TEST_ASSERT_EQUAL(-32602, parse(r)["error"]["code"].as<int>());
  r = call(R"({"jsonrpc":"2.0","id":15,"method":"tools/call","params":{"name":"fly"}})");
  TEST_ASSERT_EQUAL(-32602, parse(r)["error"]["code"].as<int>());
}

static void test_detect_and_follow() {
  auto d = parse(call(R"({"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"detect"}})"));
  TEST_ASSERT_FALSE(d["result"]["structuredContent"]["enabled"].as<bool>());
  TEST_ASSERT_NOT_NULL(strstr(d["result"]["content"][0]["text"].as<const char*>(), "detection is off"));
  d = parse(call(R"({"jsonrpc":"2.0","id":21,"method":"tools/call","params":{"name":"detect","arguments":{"enabled":true,"model":"face"}}})"));
  TEST_ASSERT_TRUE(dev.detectOn);
  TEST_ASSERT_EQUAL(1, d["result"]["structuredContent"]["objects"].size());
  const char* txt = d["result"]["content"][0]["text"];
  TEST_ASSERT_NOT_NULL(strstr(txt, "1 face in view"));
  TEST_ASSERT_NOT_NULL(strstr(txt, "right"));  // centre x 0.8
  auto r = call(R"({"jsonrpc":"2.0","id":22,"method":"tools/call","params":{"name":"detect","arguments":{"model":"dog"}}})");
  TEST_ASSERT_EQUAL(-32602, parse(r)["error"]["code"].as<int>());
  r = call(R"({"jsonrpc":"2.0","id":23,"method":"tools/call","params":{"name":"follow","arguments":{}}})");
  TEST_ASSERT_EQUAL(-32602, parse(r)["error"]["code"].as<int>());
  call(R"({"jsonrpc":"2.0","id":24,"method":"tools/call","params":{"name":"follow","arguments":{"enabled":true}}})");
  TEST_ASSERT_TRUE(dev.followOn);
  dev.detectOn = false;
  dev.followOn = false;
}

static void test_presets_and_patrol() {
  call(R"({"jsonrpc":"2.0","id":30,"method":"tools/call","params":{"name":"preset","arguments":{"action":"save","name":"door"}}})");
  auto d = parse(call(R"({"jsonrpc":"2.0","id":31,"method":"tools/call","params":{"name":"preset","arguments":{"action":"list"}}})"));
  TEST_ASSERT_EQUAL_STRING("door", d["result"]["structuredContent"]["presets"][0]["name"]);
  TEST_ASSERT_NOT_NULL(strstr(d["result"]["content"][0]["text"].as<const char*>(), "door: pan 10, tilt 40"));
  auto r = call(R"({"jsonrpc":"2.0","id":32,"method":"tools/call","params":{"name":"preset","arguments":{"action":"go","name":"attic"}}})");
  TEST_ASSERT_EQUAL(-32602, parse(r)["error"]["code"].as<int>());
  d = parse(call(R"({"jsonrpc":"2.0","id":33,"method":"tools/call","params":{"name":"patrol","arguments":{"enabled":true,"dwell_s":5}}})"));
  TEST_ASSERT_TRUE(dev.patrolOn);
  TEST_ASSERT_EQUAL_STRING("patrolling", d["result"]["content"][0]["text"].as<std::string>().substr(0, 10).c_str());
  dev.names.clear();
  dev.patrolOn = false;
}

static void test_recordings_without_a_card() {
  auto d = parse(call(R"({"jsonrpc":"2.0","id":40,"method":"tools/call","params":{"name":"recordings","arguments":{"action":"list"}}})"));
  TEST_ASSERT_TRUE(d["result"]["isError"].as<bool>());  // a tool error, not a protocol error
  d = parse(call(R"({"jsonrpc":"2.0","id":41,"method":"tools/call","params":{"name":"recordings","arguments":{"action":"status"}}})"));
  TEST_ASSERT_FALSE(d["result"]["structuredContent"]["mounted"].as<bool>());
}

static void test_base64_padding() {
  TEST_ASSERT_EQUAL_STRING("", base64((const uint8_t*)"", 0).c_str());
  TEST_ASSERT_EQUAL_STRING("Zg==", base64((const uint8_t*)"f", 1).c_str());
  TEST_ASSERT_EQUAL_STRING("Zm8=", base64((const uint8_t*)"fo", 2).c_str());
  TEST_ASSERT_EQUAL_STRING("Zm9v", base64((const uint8_t*)"foo", 3).c_str());
  TEST_ASSERT_EQUAL_STRING("Zm9vYg==", base64((const uint8_t*)"foob", 4).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_legacy_initialize_negotiates_version);
  RUN_TEST(test_notifications_are_accepted_without_body);
  RUN_TEST(test_modern_discover);
  RUN_TEST(test_modern_header_mismatch_and_unsupported);
  RUN_TEST(test_unknown_method);
  RUN_TEST(test_bad_json_and_batch);
  RUN_TEST(test_tools_list_names);
  RUN_TEST(test_look_center_sweep);
  RUN_TEST(test_snapshot_returns_image_or_tool_error);
  RUN_TEST(test_stream_url_and_camera);
  RUN_TEST(test_detect_and_follow);
  RUN_TEST(test_presets_and_patrol);
  RUN_TEST(test_recordings_without_a_card);
  RUN_TEST(test_base64_padding);
  return UNITY_END();
}
