// SPDX-License-Identifier: MIT
#include "web.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>

#include <atomic>
#include <string>

#include "app.h"
#include "auth.h"
#include "camera.h"
#include "detect.h"
#include "ota.h"
#include "sdcard.h"
#include "mcp.h"
#include "net.h"
#include "pages.h"

namespace lynx {

static httpd_handle_t api = nullptr;
static const size_t kMaxBody = 4096;

// ---------- request helpers ----------

static std::string header(httpd_req_t* r, const char* name) {
  size_t n = httpd_req_get_hdr_value_len(r, name);
  if (!n) return "";
  std::string v(n, '\0');
  httpd_req_get_hdr_value_str(r, name, &v[0], n + 1);
  return v;
}

static std::string query(httpd_req_t* r) {
  size_t n = httpd_req_get_url_query_len(r);
  if (!n) return "";
  std::string q(n, '\0');
  httpd_req_get_url_query_str(r, &q[0], n + 1);
  return q;
}

static bool body(httpd_req_t* r, std::string& out) {
  if (r->content_len > kMaxBody) return false;
  out.resize(r->content_len);
  size_t got = 0;
  while (got < r->content_len) {
    int n = httpd_req_recv(r, &out[got], r->content_len - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return false;
    got += n;
  }
  return true;
}

// True when the request arrived on the setup hotspot (192.168.4.x), i.e. from someone within radio range.
static bool viaSetupAp(httpd_req_t* r) {
  if (!setupMode()) return false;
  int fd = httpd_req_to_sockfd(r);
  struct sockaddr_storage a = {};
  socklen_t len = sizeof a;
  if (getsockname(fd, (struct sockaddr*)&a, &len) != 0) return false;
  uint32_t ip = 0;
  if (a.ss_family == AF_INET) {
    ip = ((struct sockaddr_in*)&a)->sin_addr.s_addr;
  } else if (a.ss_family == AF_INET6) {  // IPv4-mapped ::ffff:a.b.c.d
    memcpy(&ip, &((struct sockaddr_in6*)&a)->sin6_addr.un.u32_addr[3], 4);
  }
  return ip == (uint32_t)apIp();
}

static esp_err_t sendJson(httpd_req_t* r, const JsonDocument& d, const char* status = "200 OK") {
  std::string s;
  serializeJson(d, s);
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, s.data(), s.size());
}

static esp_err_t sendError(httpd_req_t* r, const char* status, const char* msg) {
  JsonDocument d;
  d["error"] = msg;
  return sendJson(r, d, status);
}

// Every API call needs the device token, except from the setup hotspot (physical proximity).
static bool authorizedReq(httpd_req_t* r) {
  if (viaSetupAp(r)) return true;
  if (authorized(header(r, "Authorization"), queryValue(query(r), "token"), settings.token.c_str())) return true;
  httpd_resp_set_hdr(r, "WWW-Authenticate", "Bearer realm=\"lynx\"");
  sendError(r, "401 Unauthorized", "missing or wrong token (Authorization: Bearer <token>, or ?token=)");
  return false;
}

static std::string baseUrl() {
  String ip = staIp();
  return std::string("http://") + (ip.length() ? mdnsName() : apIp().toString()).c_str();
}

// ---------- the MCP device ----------

class LynxDevice : public Device {
 public:
  std::string name() override { return hostName().c_str(); }
  std::string version() override { return kVersion; }
  void status(JsonObject o) override { statusJson(o); }
  uint32_t look(float pan, float tilt, int32_t ms) override {
    pauseFollow();
    MotionLock l;
    return motion.look(pan, tilt, ms, millis());
  }
  void center() override {
    pauseFollow();
    MotionLock l;
    motion.center(millis());
  }
  void sweep() override {
    pauseFollow(30000);
    MotionLock l;
    motion.startSweep(millis());
  }
  bool snapshot(std::string& jpeg, std::string& error, bool annotate) override {
    return annotate ? detectAnnotatedSnapshot(jpeg, error) : cameraSnapshot(jpeg, error);
  }
  bool setCamera(JsonObjectConst s, std::string& error) override { return applyCameraJson(s, error); }
  std::string snapshotUrl() override { return baseUrl() + "/snapshot.jpg"; }
  std::string streamUrl() override { return baseUrl() + ":81/stream"; }
  void detections(JsonObject o) override { detectionsJson(o); }
  bool setDetect(JsonObjectConst s, std::string& e) override { return applyDetectJson(s, e); }
  bool setFollow(JsonObjectConst s, std::string& e) override { return applyFollowJson(s, e); }
  void presets(JsonObject o) override { presetsJson(o); }
  bool preset(JsonObjectConst s, std::string& e) override { return presetAction(s, e); }
  bool setPatrol(JsonObjectConst s, std::string& e) override { return applyPatrolJson(s, e); }
  void recordings(JsonObject o) override { sdJson(o); }
  bool setRecording(JsonObjectConst s, std::string& e) override { return applySdJson(s, e); }
  bool listRecordings(const char* dir, JsonArray out, std::string& e) override { return sdList(dir, out, e); }
  bool readRecording(const char* path, std::string& jpeg, std::string& e) override { return sdRead(path, jpeg, e); }
};
static LynxDevice device;

// ---------- pages ----------

static esp_err_t sendPage(httpd_req_t* r, const char* html) {
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t cssHandler(httpd_req_t* r) {
  httpd_resp_set_type(r, "text/css");
  httpd_resp_set_hdr(r, "Cache-Control", "max-age=3600");
  return httpd_resp_send(r, kStyleCss, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t rootHandler(httpd_req_t* r) { return sendPage(r, viaSetupAp(r) ? kSetupHtml : kDashboardHtml); }

// Open endpoint: what this is and where to find things. No secrets.
static esp_err_t infoHandler(httpd_req_t* r) {
  JsonDocument d;
  d["name"] = hostName();
  d["id"] = deviceId();
  d["product"] = "lynx";
  d["version"] = kVersion;
  d["mdns"] = mdnsName();
  d["ip"] = staIp();
  d["setup"] = viaSetupAp(r);
  JsonObject e = d["endpoints"].to<JsonObject>();
  e["status"] = "GET /api/status";
  e["position"] = "POST /api/position {pan, tilt, ms?}";
  e["center"] = "POST /api/center";
  e["sweep"] = "POST /api/sweep";
  e["camera"] = "GET|POST /api/camera";
  e["snapshot"] = "GET /snapshot.jpg";
  e["stream"] = "GET :81/stream (several viewers)";
  e["events"] = "GET :81/events (server-sent events: the status, pushed on change)";
  e["detections"] = "GET /api/detections";
  e["detect"] = "POST /api/detect {enabled, model: face|cat}";
  e["follow"] = "POST /api/follow {enabled}";
  e["presets"] = "GET|POST /api/presets {action: save|go|delete, name}";
  e["patrol"] = "POST /api/patrol {enabled, presets, dwellS}";
  e["update"] = "GET|POST /api/update (an app image: firmware.bin; rolls back by itself if it fails)";
  e["recordings"] = "GET|POST /api/sd {onDetect, timelapseMin}; /api/sd/list?dir=; /api/sd/file?path=; POST /api/sd/delete";
  e["mcp"] = "POST /mcp";
  d["auth"] = "Authorization: Bearer <token> (or ?token= for snapshot and stream URLs)";
  return sendJson(r, d);
}

// ---------- REST ----------

static esp_err_t statusHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  JsonDocument d;
  statusJson(d.to<JsonObject>());
  return sendJson(r, d);
}

static esp_err_t positionHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  std::string b;
  JsonDocument in;
  if (!body(r, b) || deserializeJson(in, b)) return sendError(r, "400 Bad Request", "expected JSON {pan, tilt, ms?}");
  pauseFollow();
  {
    MotionLock l;
    float p = in["pan"] | motion.axis(PAN).target();
    float t = in["tilt"] | motion.axis(TILT).target();
    motion.look(p, t, in["ms"] | -1, millis());
  }
  return statusHandler(r);
}

static esp_err_t centerHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  device.center();
  return statusHandler(r);
}

static esp_err_t sweepHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  device.sweep();
  return statusHandler(r);
}

static esp_err_t cameraGetHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  JsonDocument d;
  cameraJson(d.to<JsonObject>());
  return sendJson(r, d);
}

static esp_err_t cameraPostHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  std::string b, err;
  JsonDocument in;
  if (!body(r, b) || deserializeJson(in, b)) return sendError(r, "400 Bad Request", "expected JSON");
  if (!applyCameraJson(in.as<JsonObjectConst>(), err)) return sendError(r, "400 Bad Request", err.c_str());
  return cameraGetHandler(r);
}

static esp_err_t detectionsHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  JsonDocument d;
  detectionsJson(d.to<JsonObject>());
  return sendJson(r, d);
}

// POST /api/detect/image (a JPEG body): run the current model on it, for testing and tuning
static esp_err_t detectImageHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  if (!detectEnabled()) return sendError(r, "409 Conflict", "turn detection on first (POST /api/detect)");
  if (r->content_len == 0 || r->content_len > 400000) return sendError(r, "413 Payload Too Large", "send one JPEG, up to 400 kB");
  uint8_t* buf = (uint8_t*)ps_malloc(r->content_len);
  if (!buf) return sendError(r, "503 Service Unavailable", "out of memory");
  size_t got = 0;
  while (got < r->content_len) {
    int n = httpd_req_recv(r, (char*)buf + got, r->content_len - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) { free(buf); return ESP_FAIL; }
    got += n;
  }
  Detections d;
  bool dbg = queryValue(query(r), "debug") == "1";
  detectDebugWantImage(dbg);
  bool ok = detectImage(buf, got, d);
  detectDebugWantImage(false);
  free(buf);
  uint8_t* img = nullptr;
  size_t imgLen = 0;
  int stage1 = 0;
  detectDebugTake(&img, &imgLen, &stage1);
  if (dbg && img) {  // ?debug=1: answer with the frame the model saw; counts in headers
    char hdr[48];
    snprintf(hdr, sizeof hdr, "%d", d.n);
    httpd_resp_set_type(r, "image/jpeg");
    httpd_resp_set_hdr(r, "X-Lynx-Objects", hdr);
    char h2[16];
    snprintf(h2, sizeof h2, "%d", stage1);
    httpd_resp_set_hdr(r, "X-Lynx-Stage1", h2);
    esp_err_t e = httpd_resp_send(r, (const char*)img, imgLen);
    free(img);
    return e;
  }
  free(img);
  if (!ok) return sendError(r, "422 Unprocessable Entity", "couldn't run detection on that image (not a baseline JPEG?)");
  JsonDocument out;
  out["model"] = detectModelName(d.model);
  out["frame"]["w"] = d.inW;
  out["frame"]["h"] = d.inH;
  JsonArray a = out["objects"].to<JsonArray>();
  for (int i = 0; i < d.n; i++) {
    JsonObject b = a.add<JsonObject>();
    b["label"] = d.label[i] ? d.label[i] : detectModelName(d.model);
    b["score"] = roundf(d.box[i].score * 100) / 100;
    JsonArray box = b["box"].to<JsonArray>();
    for (float v : {d.box[i].x, d.box[i].y, d.box[i].w, d.box[i].h}) box.add(roundf(v * 1000) / 1000);
  }
  return sendJson(r, out);
}

// POST /api/detect {enabled, model} and /api/follow {enabled, panSign, tiltSign}
static esp_err_t detectSetHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  std::string b, err;
  JsonDocument in;
  if (!body(r, b) || deserializeJson(in, b)) return sendError(r, "400 Bad Request", "expected JSON {enabled, model}");
  if (!applyDetectJson(in.as<JsonObjectConst>(), err)) return sendError(r, "400 Bad Request", err.c_str());
  return detectionsHandler(r);
}

static esp_err_t followSetHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  std::string b, err;
  JsonDocument in;
  if (!body(r, b) || deserializeJson(in, b)) return sendError(r, "400 Bad Request", "expected JSON {enabled}");
  if (!applyFollowJson(in.as<JsonObjectConst>(), err)) return sendError(r, "400 Bad Request", err.c_str());
  return detectionsHandler(r);
}

// recordings: GET|POST /api/sd; GET /api/sd/list?dir=; GET /api/sd/file?path=; POST /api/sd/delete {path}
static std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else o += s[i] == '+' ? ' ' : s[i];
  }
  return o;
}
static esp_err_t sdGetHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  JsonDocument d;
  sdJson(d.to<JsonObject>());
  return sendJson(r, d);
}
static esp_err_t sdPostHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  std::string b, err;
  JsonDocument in;
  if (!body(r, b) || deserializeJson(in, b)) return sendError(r, "400 Bad Request", "expected JSON {onDetect, timelapseMin}");
  if (!applySdJson(in.as<JsonObjectConst>(), err)) return sendError(r, "409 Conflict", err.c_str());
  return sdGetHandler(r);
}
static esp_err_t sdListHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  std::string dir = urlDecode(queryValue(query(r), "dir"));
  if (dir.empty()) dir = "/lynx";
  JsonDocument d;
  d["dir"] = dir;
  std::string err;
  if (!sdList(dir.c_str(), d["items"].to<JsonArray>(), err)) return sendError(r, "404 Not Found", err.c_str());
  return sendJson(r, d);
}
static esp_err_t sdFileHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  return sdSendFile(r, urlDecode(queryValue(query(r), "path")).c_str());
}
static esp_err_t sdDeleteHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  std::string b, err;
  JsonDocument in;
  if (!body(r, b) || deserializeJson(in, b) || !in["path"].is<const char*>())
    return sendError(r, "400 Bad Request", "expected JSON {path}");
  if (!sdDelete(in["path"].as<const char*>(), err)) return sendError(r, "404 Not Found", err.c_str());
  return sdGetHandler(r);
}

// firmware: GET /api/update (version, slots); POST /api/update (an app image: firmware.bin)
static esp_err_t updateGetHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  JsonDocument d;
  otaJson(d.to<JsonObject>());
  return sendJson(r, d);
}
static esp_err_t updatePostHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  return otaUpload(r);
}

// presets + patrol: GET /api/presets; POST /api/presets {action, name, ...}; POST /api/patrol {...}
static esp_err_t presetsGetHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  JsonDocument d;
  presetsJson(d.to<JsonObject>());
  return sendJson(r, d);
}

static esp_err_t jsonPost(httpd_req_t* r, bool (*apply)(JsonObjectConst, std::string&), const char* shape) {
  if (!authorizedReq(r)) return ESP_OK;
  std::string b, err;
  JsonDocument in;
  if (!body(r, b) || deserializeJson(in, b)) return sendError(r, "400 Bad Request", shape);
  if (!apply(in.as<JsonObjectConst>(), err)) return sendError(r, "400 Bad Request", err.c_str());
  return presetsGetHandler(r);
}
static esp_err_t presetsPostHandler(httpd_req_t* r) {
  return jsonPost(r, presetAction, "expected JSON {action: save|go|delete, name, pan?, tilt?}");
}
static esp_err_t patrolPostHandler(httpd_req_t* r) {
  return jsonPost(r, applyPatrolJson, "expected JSON {enabled, presets: [names], dwellS}");
}

static esp_err_t snapshotHandler(httpd_req_t* r) {
  if (!authorizedReq(r)) return ESP_OK;
  camera_fb_t* fb = cameraGrab();
  if (!fb) return sendError(r, "503 Service Unavailable", "camera not ready");
  httpd_resp_set_type(r, "image/jpeg");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_hdr(r, "Content-Disposition", "inline; filename=lynx.jpg");
  esp_err_t e = httpd_resp_send(r, (const char*)fb->buf, fb->len);
  cameraRelease(fb);
  return e;
}

// ---------- MCP ----------

static esp_err_t mcpHandler(httpd_req_t* r) {
  // DNS-rebinding guard (MCP transport spec): a browser Origin must name this device
  std::string origin = header(r, "Origin");
  if (!originAllowed(origin, mdnsName().c_str(), staIp().c_str()) &&
      !(viaSetupAp(r) && originHost(origin) == apIp().toString().c_str()))
    return sendError(r, "403 Forbidden", "origin not allowed");
  if (!authorizedReq(r)) return ESP_OK;
  McpRequest q;
  if (!body(r, q.body)) return sendError(r, "413 Payload Too Large", "request too large");
  q.protocolHeader = header(r, "MCP-Protocol-Version");
  q.methodHeader = header(r, "Mcp-Method");
  q.nameHeader = header(r, "Mcp-Name");
  McpResponse resp = handleMcp(device, q);
  char status[32];
  snprintf(status, sizeof status, "%d %s", resp.status,
           resp.status == 200   ? "OK"
           : resp.status == 202 ? "Accepted"
           : resp.status == 404 ? "Not Found"
                                : "Bad Request");
  httpd_resp_set_status(r, status);
  if (resp.body.empty()) return httpd_resp_send(r, nullptr, 0);
  httpd_resp_set_type(r, "application/json");
  return httpd_resp_send(r, resp.body.data(), resp.body.size());
}

static esp_err_t mcpNoStream(httpd_req_t* r) {
  httpd_resp_set_status(r, "405 Method Not Allowed");
  httpd_resp_set_hdr(r, "Allow", "POST");
  return httpd_resp_send(r, nullptr, 0);
}

// ---------- setup (hotspot only) ----------

static esp_err_t scanHandler(httpd_req_t* r) {
  if (!viaSetupAp(r)) return sendError(r, "403 Forbidden", "setup is only available on the setup hotspot");
  int n = WiFi.scanNetworks();
  JsonDocument d;
  JsonArray a = d["networks"].to<JsonArray>();
  for (int i = 0; i < n && i < 20; i++) {
    bool dup = false;
    for (JsonObject o : a) dup |= WiFi.SSID(i) == (const char*)o["ssid"];
    if (dup || !WiFi.SSID(i).length()) continue;
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  return sendJson(r, d);
}

static esp_err_t joinHandler(httpd_req_t* r) {
  if (!viaSetupAp(r)) return sendError(r, "403 Forbidden", "setup is only available on the setup hotspot");
  std::string b;
  JsonDocument in;
  if (!body(r, b) || deserializeJson(in, b) || !in["ssid"].is<const char*>())
    return sendError(r, "400 Bad Request", "expected JSON {ssid, pass}");
  joinWifi(in["ssid"].as<const char*>(), in["pass"] | "");
  JsonDocument d;
  d["state"] = wifiState();
  return sendJson(r, d);
}

static esp_err_t setupStateHandler(httpd_req_t* r) {
  if (!viaSetupAp(r)) return sendError(r, "403 Forbidden", "setup is only available on the setup hotspot");
  JsonDocument d;
  d["state"] = wifiState();
  d["ssid"] = settings.ssid;
  d["name"] = hostName();
  d["mdns"] = mdnsName();
  d["ip"] = staIp();
  d["token"] = settings.token;  // shown only here, to someone within radio range of the device
  d["version"] = kVersion;
  return sendJson(r, d);
}

// Captive portal: on the hotspot, anything unknown (generate_204, hotspot-detect.html, ...) goes to setup.
static esp_err_t notFound(httpd_req_t* r, httpd_err_code_t) {
  if (viaSetupAp(r)) {
    std::string to = std::string("http://") + apIp().toString().c_str() + "/";
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", to.c_str());
    return httpd_resp_send(r, nullptr, 0);
  }
  sendError(r, "404 Not Found", "not found (see /api/info)");
  return ESP_OK;
}

// ---------- start ----------

static void route(httpd_handle_t s, const char* uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t*)) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = m;
  u.handler = h;
  httpd_register_uri_handler(s, &u);
}

void webBegin() {
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.server_port = 80;
  c.ctrl_port = 32768;
  c.max_uri_handlers = 40;
  c.stack_size = 8192;
  c.recv_wait_timeout = 15;  // an OTA upload over slow Wi-Fi
  c.lru_purge_enable = true;
  c.max_open_sockets = 7;
  if (httpd_start(&api, &c) == ESP_OK) {
    route(api, "/", HTTP_GET, rootHandler);
    route(api, "/lynx.css", HTTP_GET, cssHandler);
    route(api, "/api/info", HTTP_GET, infoHandler);
    route(api, "/api/status", HTTP_GET, statusHandler);
    route(api, "/api/position", HTTP_POST, positionHandler);
    route(api, "/api/look", HTTP_POST, positionHandler);  // 0.1 name
    route(api, "/api/center", HTTP_POST, centerHandler);
    route(api, "/api/sweep", HTTP_POST, sweepHandler);
    route(api, "/api/camera", HTTP_GET, cameraGetHandler);
    route(api, "/api/camera", HTTP_POST, cameraPostHandler);
    route(api, "/snapshot.jpg", HTTP_GET, snapshotHandler);
    route(api, "/api/detections", HTTP_GET, detectionsHandler);
    route(api, "/api/presets", HTTP_GET, presetsGetHandler);
    route(api, "/api/update", HTTP_GET, updateGetHandler);
    route(api, "/api/sd", HTTP_GET, sdGetHandler);
    route(api, "/api/sd", HTTP_POST, sdPostHandler);
    route(api, "/api/sd/list", HTTP_GET, sdListHandler);
    route(api, "/api/sd/file", HTTP_GET, sdFileHandler);
    route(api, "/api/sd/delete", HTTP_POST, sdDeleteHandler);
    route(api, "/api/update", HTTP_POST, updatePostHandler);
    route(api, "/api/presets", HTTP_POST, presetsPostHandler);
    route(api, "/api/patrol", HTTP_POST, patrolPostHandler);
    route(api, "/api/detect", HTTP_POST, detectSetHandler);
    route(api, "/api/detect/image", HTTP_POST, detectImageHandler);
    route(api, "/api/follow", HTTP_POST, followSetHandler);
    route(api, "/api/snapshot", HTTP_GET, snapshotHandler);
    route(api, "/mcp", HTTP_POST, mcpHandler);
    route(api, "/mcp", HTTP_GET, mcpNoStream);
    route(api, "/mcp", HTTP_DELETE, mcpNoStream);
    route(api, "/setup/scan", HTTP_GET, scanHandler);
    route(api, "/setup/wifi", HTTP_POST, joinHandler);
    route(api, "/setup/state", HTTP_GET, setupStateHandler);
    httpd_register_err_handler(api, HTTPD_404_NOT_FOUND, notFound);
  } else {
    Serial.println("http server failed to start");
  }
}

}  // namespace lynx
