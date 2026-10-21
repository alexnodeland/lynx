// SPDX-License-Identifier: MIT
#include "stream.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>

#include <atomic>
#include <cstring>
#include <string>

#include "app.h"
#include "auth.h"
#include "camera.h"

namespace lynx {

static const int kPort = 81;
static const int kMaxClients = 4;
static const int kSendTimeoutS = 3;  // a viewer that can't take a frame in 3 s is dropped
static const char* kBoundary = "lynxframe";

static std::atomic<int> clients{0}, streamViewers{0};
static std::atomic<uint32_t> eventGen{1};

// the shared frame: one producer writes, viewers copy it out under the lock
static SemaphoreHandle_t frameLock;
static uint8_t* frame = nullptr;
static size_t frameLen = 0, frameCap = 0;
static std::atomic<uint32_t> frameSeq{0};
static TaskHandle_t waiters[kMaxClients] = {};
static portMUX_TYPE waitersMux = portMUX_INITIALIZER_UNLOCKED;

int streamClients() { return clients; }
uint32_t streamFrameSeq() { return frameSeq; }
void eventsPoke() { eventGen++; }

static void addWaiter(TaskHandle_t t) {
  portENTER_CRITICAL(&waitersMux);
  for (auto& w : waiters)
    if (!w) { w = t; break; }
  portEXIT_CRITICAL(&waitersMux);
}
static void removeWaiter(TaskHandle_t t) {
  portENTER_CRITICAL(&waitersMux);
  for (auto& w : waiters)
    if (w == t) w = nullptr;
  portEXIT_CRITICAL(&waitersMux);
}

// takes frames only while someone is watching
static void producer(void*) {
  for (;;) {
    if (streamViewers == 0 || !cameraReady()) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    camera_fb_t* fb = cameraGrab();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    xSemaphoreTake(frameLock, portMAX_DELAY);
    if (fb->len > frameCap) {
      free(frame);
      frameCap = fb->len + 16384;
      frame = (uint8_t*)ps_malloc(frameCap);
      if (!frame) frameCap = 0;
    }
    if (frame) {
      memcpy(frame, fb->buf, fb->len);
      frameLen = fb->len;
      frameSeq++;
    }
    xSemaphoreGive(frameLock);
    cameraRelease(fb);
    portENTER_CRITICAL(&waitersMux);
    for (auto& w : waiters)
      if (w) xTaskNotifyGive(w);
    portEXIT_CRITICAL(&waitersMux);
    vTaskDelay(1);
  }
}

static bool sendAll(int fd, const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  while (len) {
    int n = send(fd, p, len, 0);
    if (n <= 0) return false;
    p += n;
    len -= n;
  }
  return true;
}
static bool sendStr(int fd, const char* s) { return sendAll(fd, s, strlen(s)); }

static void reply(int fd, const char* status, const char* msg) {
  char b[256];
  snprintf(b, sizeof b,
           "HTTP/1.1 %s\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n"
           "\r\n{\"error\":\"%s\"}",
           status, msg);
  sendStr(fd, b);
}

struct Request {
  std::string method, path, query, authorization;
};

// read the request line and headers (up to 2 kB, with the socket's receive timeout)
static bool readRequest(int fd, Request& r) {
  char buf[2048];
  size_t n = 0;
  while (n < sizeof buf - 1) {
    int k = recv(fd, buf + n, sizeof buf - 1 - n, 0);
    if (k <= 0) return false;
    n += k;
    buf[n] = 0;
    if (strstr(buf, "\r\n\r\n")) break;
  }
  buf[n] = 0;
  char* line_end = strstr(buf, "\r\n");
  if (!line_end) return false;
  *line_end = 0;
  char* sp1 = strchr(buf, ' ');
  if (!sp1) return false;
  *sp1 = 0;
  char* target = sp1 + 1;
  char* sp2 = strchr(target, ' ');
  if (sp2) *sp2 = 0;
  r.method = buf;
  char* q = strchr(target, '?');
  if (q) {
    *q = 0;
    r.query = q + 1;
  }
  r.path = target;
  for (char* h = line_end + 2; *h;) {
    char* e = strstr(h, "\r\n");
    if (!e) break;
    *e = 0;
    if (!strncasecmp(h, "Authorization:", 14)) {
      const char* v = h + 14;
      while (*v == ' ') v++;
      r.authorization = v;
    }
    h = e + 2;
  }
  return true;
}

static void serveStream(int fd, const Request& r) {
  int fps = atoi(queryValue(r.query, "fps").c_str());
  uint32_t minGap = fps > 0 ? 1000 / (fps > 30 ? 30 : fps) : 0;
  char head[256];
  snprintf(head, sizeof head,
           "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=%s\r\nCache-Control: no-store\r\n"
           "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
           kBoundary);
  if (!sendStr(fd, head)) return;
  TaskHandle_t me = xTaskGetCurrentTaskHandle();
  addWaiter(me);
  streamViewers++;
  uint8_t* mine = nullptr;
  size_t cap = 0, len = 0;
  uint32_t seen = 0, last = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    if (frameSeq == seen) continue;
    if (minGap && millis() - last < minGap) continue;
    xSemaphoreTake(frameLock, portMAX_DELAY);  // copy out quickly, send outside the lock
    if (frameLen > cap) {
      free(mine);
      cap = frameLen + 16384;
      mine = (uint8_t*)ps_malloc(cap);
    }
    if (mine) {
      memcpy(mine, frame, frameLen);
      len = frameLen;
    }
    seen = frameSeq;
    xSemaphoreGive(frameLock);
    if (!mine) break;
    last = millis();
    char part[96];
    int n = snprintf(part, sizeof part, "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", kBoundary,
                     (unsigned)len);
    if (!sendAll(fd, part, n) || !sendAll(fd, mine, len)) break;
  }
  streamViewers--;
  removeWaiter(me);
  free(mine);
}

// The status as one event, whenever something changed (eventsPoke, or the summary below moved), and a
// comment line every 15 s so proxies and clients know the connection is alive.
static void serveEvents(int fd) {
  if (!sendStr(fd,
               "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\n"
               "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\nretry: 3000\n\n"))
    return;
  uint32_t gen = 0, lastSend = 0, lastPing = millis();
  std::string lastKey;
  for (;;) {
    JsonDocument s;
    statusJson(s.to<JsonObject>());
    // what counts as a change: not uptime or heap, which tick every time
    char key[192];
    snprintf(key, sizeof key, "%.0f|%.0f|%d|%d|%d|%d|%d|%s|%d|%s", s["pan"]["target"].as<float>(),
             s["tilt"]["target"].as<float>(), (int)s["pan"]["moving"].as<bool>(), (int)s["tilt"]["moving"].as<bool>(),
             (int)s["detect"]["present"].as<bool>(), s["detect"]["count"].as<int>(),
             (int)s["detect"]["tracking"].as<bool>(), s["patrol"]["at"] | "", (int)s["sweeping"].as<bool>(),
             s["camera"]["framesize"] | "");
    uint32_t now = millis();
    bool changed = lastKey != key || gen != eventGen.load();
    if (changed && now - lastSend >= 150) {  // at most ~6 events/s
      std::string json;
      serializeJson(s, json);  // (serializeJson replaces a string's contents, so build the frame around it)
      std::string out = "event: status\ndata: " + json + "\n\n";
      if (!sendAll(fd, out.data(), out.size())) return;
      lastKey = key;
      gen = eventGen;
      lastSend = now;
    }
    if (now - lastPing > 15000) {
      if (!sendStr(fd, ": ping\n\n")) return;
      lastPing = now;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

static void client(void* arg) {
  int fd = (int)(intptr_t)arg;
  timeval tv = {kSendTimeoutS, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  timeval rtv = {3, 0};  // a half-open connection can't hold a slot
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof rtv);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  Request r;
  if (!readRequest(fd, r)) {
    // nothing sensible arrived
  } else if (r.method != "GET") {
    reply(fd, "405 Method Not Allowed", "GET only");
  } else if (r.path != "/stream" && r.path != "/events") {
    reply(fd, "404 Not Found", "port 81 serves /stream and /events");
  } else if (!authorized(r.authorization, queryValue(r.query, "token"), settings.token.c_str())) {
    reply(fd, "401 Unauthorized", "missing or wrong token (Authorization: Bearer <token>, or ?token=)");
  } else if (r.path == "/stream") {
    serveStream(fd, r);
  } else {
    serveEvents(fd);
  }
  shutdown(fd, SHUT_RDWR);
  close(fd);
  clients--;
  vTaskDelete(nullptr);
}

static void acceptor(void*) {
  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(kPort);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 4) != 0) {
    Serial.println("stream: can't listen on :81");
    vTaskDelete(nullptr);
    return;
  }
  for (;;) {
    int fd = accept(s, nullptr, nullptr);
    if (fd < 0) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (clients >= kMaxClients) {
      timeval tv = {1, 0};
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
      reply(fd, "503 Service Unavailable", "4 viewers already; try /snapshot.jpg on port 80");
      close(fd);
      continue;
    }
    clients++;
    if (xTaskCreatePinnedToCore(client, "stream-client", 6144, (void*)(intptr_t)fd, 2, nullptr, 0) != pdPASS) {
      clients--;
      close(fd);
    }
  }
}

void streamBegin() {
  frameLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(producer, "stream-frames", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(acceptor, "stream-accept", 4096, nullptr, 2, nullptr, 0);
}

}  // namespace lynx
