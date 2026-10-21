// SPDX-License-Identifier: MIT
#include "sdcard.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/semphr.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "app.h"
#include "camera.h"
#include "detect.h"
#include "ota.h"
#include "recorder.h"

namespace lynx {

static const int kCs = 21;  // XIAO ESP32-S3 Sense: SD on SPI (SCK 7, MISO 8, MOSI 9), CS 21 (shared with the user LED)
static const char* kRoot = "/lynx";
static SemaphoreHandle_t lock;
static bool mounted = false;
static Recorder recorder;
static uint32_t saved = 0, deleted = 0, failed = 0;
static std::string lastFile;

bool sdMounted() { return mounted; }

struct SdLock {
  SdLock() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~SdLock() { xSemaphoreGive(lock); }
};

static bool clockValid() { return time(nullptr) > 1700000000; }

// /lynx/YYYYMMDD/HHMMSS_kind.jpg (UTC); before the clock is set, /lynx/00000000/<uptime s>_kind.jpg
static std::string pathFor(const char* kind) {
  char dir[24], file[48];
  if (clockValid()) {
    time_t t = time(nullptr);
    tm u;
    gmtime_r(&t, &u);
    strftime(dir, sizeof dir, "/lynx/%Y%m%d", &u);
    snprintf(file, sizeof file, "%s/%02d%02d%02d_%s.jpg", dir, u.tm_hour, u.tm_min, u.tm_sec, kind);
  } else {
    strcpy(dir, "/lynx/00000000");
    snprintf(file, sizeof file, "%s/%06lu_%s.jpg", dir, (unsigned long)(millis() / 1000), kind);
  }
  if (!SD.exists(kRoot)) SD.mkdir(kRoot);
  if (!SD.exists(dir)) SD.mkdir(dir);
  // two shots in the same second: add a suffix
  std::string p = file;
  for (int i = 1; SD.exists(p.c_str()) && i < 10; i++) {
    std::string base(file);
    p = base.substr(0, base.size() - 4) + "-" + std::to_string(i) + ".jpg";
  }
  return p;
}

static void save(const char* kind) {
  std::string jpeg, err;
  if (!cameraSnapshot(jpeg, err)) {
    failed++;
    return;
  }
  SdLock l;
  std::string p = pathFor(kind);
  File f = SD.open(p.c_str(), FILE_WRITE);
  if (!f || f.write((const uint8_t*)jpeg.data(), jpeg.size()) != jpeg.size()) {
    failed++;
    if (f) f.close();
    return;
  }
  f.close();
  saved++;
  lastFile = p;
}

// all recordings, oldest first (names sort by date and time)
static void collect(std::vector<RecFile>& out) {
  File root = SD.open(kRoot);
  if (!root) return;
  std::vector<std::string> days;
  for (File d = root.openNextFile(); d; d = root.openNextFile()) {
    if (d.isDirectory()) days.push_back(std::string(kRoot) + "/" + d.name());
    d.close();
  }
  std::sort(days.begin(), days.end());
  for (auto& day : days) {
    File dir = SD.open(day.c_str());
    std::vector<RecFile> files;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      RecFile r;
      snprintf(r.path, sizeof r.path, "%s/%s", day.c_str(), f.name());
      r.size = f.size();
      if (!f.isDirectory()) files.push_back(r);
      f.close();
    }
    dir.close();
    std::sort(files.begin(), files.end(), [](const RecFile& a, const RecFile& b) { return strcmp(a.path, b.path) < 0; });
    out.insert(out.end(), files.begin(), files.end());
  }
}

static void retention() {
  SdLock l;
  std::vector<RecFile> files;
  collect(files);
  int k = retentionCount(files.data(), files.size(), SD.usedBytes(), SD.totalBytes(), recorder.policy());
  for (int i = 0; i < k; i++)
    if (SD.remove(files[i].path)) deleted++;
}

static void task(void*) {
  uint32_t lastRetention = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(300));
    if (!mounted || otaInProgress()) continue;
    uint32_t now = millis(), seen = detectLastSeenMs();
    bool present = detectEnabled() && seen && now - seen < 2000;
    ShotKind k = recorder.tick(now, present);
    if (k == ShotKind::Event) save(detectModelName(detectModel()));
    else if (k == ShotKind::Timelapse) save("timelapse");
    if (now - lastRetention > 60000) {
      lastRetention = now;
      retention();
    }
  }
}

void sdBegin() {
  lock = xSemaphoreCreateMutex();
  recorder.policy().onDetect = settings.recOnDetect;
  recorder.policy().timelapseMs = (uint32_t)settings.recTimelapseMin * 60000u;
  configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC; it syncs once Wi-Fi is up
  mounted = SD.begin(kCs, SPI, 20000000, "/sd", 5);
  if (mounted && SD.cardType() == CARD_NONE) {
    SD.end();
    mounted = false;
  }
  Serial.printf("sd: %s\n", mounted ? "card mounted" : "no card");
  xTaskCreatePinnedToCore(task, "recorder", 6144, nullptr, 1, nullptr, 0);
}

void sdJson(JsonObject o) {
  o["mounted"] = mounted;
  o["onDetect"] = recorder.policy().onDetect;
  o["timelapseMin"] = recorder.policy().timelapseMs / 60000;
  o["clockSet"] = clockValid();
  o["saved"] = saved;
  o["deleted"] = deleted;
  o["failed"] = failed;
  if (!mounted) return;
  SdLock l;
  o["totalMB"] = (double)SD.totalBytes() / 1048576.0;
  o["usedMB"] = (double)SD.usedBytes() / 1048576.0;
  if (!lastFile.empty()) o["last"] = lastFile;
}

bool applySdJson(JsonObjectConst in, std::string& error) {
  if (!in["timelapseMin"].isNull()) {
    int m = in["timelapseMin"].as<int>();
    if (m < 0 || m > 1440) {
      error = "timelapseMin is 0 (off) to 1440";
      return false;
    }
    settings.recTimelapseMin = m;
  }
  if (!in["onDetect"].isNull()) settings.recOnDetect = in["onDetect"].as<bool>();
  recorder.policy().onDetect = settings.recOnDetect;
  recorder.policy().timelapseMs = (uint32_t)settings.recTimelapseMin * 60000u;
  saveRecording(settings);
  if (!mounted && (settings.recOnDetect || settings.recTimelapseMin)) {
    error = "saved, but there's no microSD card: insert one and reboot";
    return false;
  }
  return true;
}

bool sdList(const char* dir, JsonArray out, std::string& error) {
  if (!mounted) {
    error = "no microSD card";
    return false;
  }
  if (!recPathSafe(dir)) {
    error = "path must be under /lynx";
    return false;
  }
  SdLock l;
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) {
    error = "no such folder";
    return false;
  }
  std::vector<std::pair<std::string, uint64_t>> items;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    items.push_back({std::string(f.name()) + (f.isDirectory() ? "/" : ""), f.isDirectory() ? 0 : f.size()});
    f.close();
  }
  d.close();
  std::sort(items.begin(), items.end());
  for (auto& it : items) {
    JsonObject o = out.add<JsonObject>();
    o["name"] = it.first;
    if (it.first.back() != '/') o["bytes"] = it.second;
  }
  return true;
}

esp_err_t sdSendFile(httpd_req_t* r, const char* path) {
  auto err = [&](const char* st, const char* m) {
    httpd_resp_set_status(r, st);
    httpd_resp_set_type(r, "application/json");
    std::string b = std::string("{\"error\":\"") + m + "\"}";
    return httpd_resp_send(r, b.data(), b.size());
  };
  if (!mounted) return err("503 Service Unavailable", "no microSD card");
  if (!recPathSafe(path) || !strstr(path, ".jpg")) return err("400 Bad Request", "path must be a .jpg under /lynx");
  SdLock l;
  File f = SD.open(path);
  if (!f || f.isDirectory()) return err("404 Not Found", "no such recording");
  httpd_resp_set_type(r, "image/jpeg");
  static uint8_t buf[4096];
  for (;;) {
    int n = f.read(buf, sizeof buf);
    if (n <= 0) break;
    if (httpd_resp_send_chunk(r, (const char*)buf, n) != ESP_OK) break;
  }
  f.close();
  return httpd_resp_send_chunk(r, nullptr, 0);
}

bool sdRead(const char* path, std::string& jpeg, std::string& error) {
  if (!mounted || !recPathSafe(path) || !strstr(path, ".jpg")) {
    error = mounted ? "path must be a .jpg under /lynx" : "no microSD card";
    return false;
  }
  SdLock l;
  File f = SD.open(path);
  if (!f || f.isDirectory() || f.size() > 600000) {
    error = "no such recording";
    return false;
  }
  jpeg.resize(f.size());
  f.read((uint8_t*)&jpeg[0], jpeg.size());
  f.close();
  return true;
}

bool sdDelete(const char* path, std::string& error) {
  if (!mounted || !recPathSafe(path) || !strcmp(path, kRoot)) {
    error = mounted ? "path must be a recording or a day under /lynx" : "no microSD card";
    return false;
  }
  SdLock l;
  File f = SD.open(path);
  if (!f) {
    error = "no such recording";
    return false;
  }
  bool dir = f.isDirectory();
  std::vector<std::string> inside;
  if (dir)
    for (File c = f.openNextFile(); c; c = f.openNextFile()) {
      inside.push_back(std::string(path) + "/" + c.name());
      c.close();
    }
  f.close();
  for (auto& p : inside) SD.remove(p.c_str());
  bool ok = dir ? SD.rmdir(path) : SD.remove(path);
  if (!ok) error = "couldn't delete it";
  return ok;
}

}  // namespace lynx
