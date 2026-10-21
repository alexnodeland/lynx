// SPDX-License-Identifier: MIT
#include "ota.h"

#include <Arduino.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include <atomic>
#include <cstring>
#include <string>

#include "app.h"

// Arduino marks every image valid at boot unless this says otherwise; lynx verifies its own.
extern "C" bool verifyRollbackLater() { return true; }

namespace lynx {

static const uint32_t kHealthyAfterMs = 45000;
static std::atomic<bool> updating{false};
static bool pending = false, confirmed = false;
static uint32_t bootMs = 0;

bool otaInProgress() { return updating; }

void otaBegin() {
  bootMs = millis();
  esp_ota_img_states_t st;
  const esp_partition_t* run = esp_ota_get_running_partition();
  pending = esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if (!pending) {
    esp_ota_mark_app_valid_cancel_rollback();  // flashed over USB, or already confirmed: nothing to prove
    confirmed = true;
  } else {
    Serial.printf("ota: new image in %s, confirming after %u s of healthy running\n", run->label,
                  (unsigned)(kHealthyAfterMs / 1000));
  }
}

void otaLoop() {
  if (confirmed || millis() - bootMs < kHealthyAfterMs) return;
  // the loop is turning and the servers have been up for the whole window: keep this image
  esp_ota_mark_app_valid_cancel_rollback();
  confirmed = true;
  pending = false;
  Serial.println("ota: new image confirmed");
}

void otaJson(JsonObject o) {
  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  o["version"] = kVersion;
  o["running"] = run ? run->label : "?";
  o["next"] = next ? next->label : "?";
  o["slotBytes"] = next ? next->size : 0;
  o["pendingVerify"] = pending;
  o["rollback"] = true;
  o["updating"] = (bool)updating;
}

static bool hex32(const std::string& h, uint8_t out[32]) {
  if (h.size() != 64) return false;
  for (int i = 0; i < 32; i++) {
    unsigned v;
    if (sscanf(h.c_str() + 2 * i, "%2x", &v) != 1) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

static esp_err_t fail(httpd_req_t* r, const char* status, const char* msg) {
  Update.abort();
  updating = false;
  char b[200];
  snprintf(b, sizeof b, "{\"error\":\"%s\"}", msg);
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json");
  return httpd_resp_send(r, b, HTTPD_RESP_USE_STRLEN);
}

static void rebootSoon(void*) {
  vTaskDelay(pdMS_TO_TICKS(800));  // let the reply reach the client first
  ESP.restart();
}

esp_err_t otaUpload(httpd_req_t* r) {
  if (updating.exchange(true)) return fail(r, "409 Conflict", "an update is already running");
  size_t total = r->content_len;
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next) return fail(r, "500 Internal Server Error", "no OTA slot in this partition table");
  if (total < 1024 || total > next->size) return fail(r, "413 Payload Too Large", "not an app image, or bigger than the slot");

  // optional integrity check: X-Lynx-SHA256: <64 hex>
  uint8_t want[32];
  bool checkHash = false;
  size_t hl = httpd_req_get_hdr_value_len(r, "X-Lynx-SHA256");
  if (hl) {
    std::string h(hl, '\0');
    httpd_req_get_hdr_value_str(r, "X-Lynx-SHA256", &h[0], hl + 1);
    if (!hex32(h, want)) return fail(r, "400 Bad Request", "X-Lynx-SHA256 must be 64 hex characters");
    checkHash = true;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  if (!Update.begin(total, U_FLASH)) return fail(r, "500 Internal Server Error", "can't start the update");
  static uint8_t buf[4096];
  size_t got = 0;
  while (got < total) {
    int n = httpd_req_recv(r, (char*)buf, sizeof buf < total - got ? sizeof buf : total - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) {
      mbedtls_sha256_free(&sha);
      return fail(r, "400 Bad Request", "upload interrupted");
    }
    if (got == 0) {
      // an ESP32-S3 app image: magic byte, and the chip id in the extended header (bytes 12-13)
      if (buf[0] != 0xE9) {
        mbedtls_sha256_free(&sha);
        return fail(r, "415 Unsupported Media Type", "not an ESP32 app image (send firmware.bin, not lynx.bin)");
      }
      if (n >= 14 && (buf[12] | (buf[13] << 8)) != 9) {
        mbedtls_sha256_free(&sha);
        return fail(r, "415 Unsupported Media Type", "this image is for a different chip, not the ESP32-S3");
      }
      // an app (not the bootloader that starts the merged web-flasher image): esp_app_desc_t magic at offset 32
      uint32_t magic = n >= 36 ? (buf[32] | (buf[33] << 8) | (buf[34] << 16) | ((uint32_t)buf[35] << 24)) : 0;
      if (magic != 0xABCD5432) {
        mbedtls_sha256_free(&sha);
        return fail(r, "415 Unsupported Media Type", "not an app image: send firmware.bin / lynx-app-*.bin, not the merged lynx.bin");
      }
    }
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != (size_t)n) {
      mbedtls_sha256_free(&sha);
      return fail(r, "500 Internal Server Error", "flash write failed");
    }
    got += n;
  }
  uint8_t have[32];
  mbedtls_sha256_finish(&sha, have);
  mbedtls_sha256_free(&sha);
  if (checkHash && memcmp(have, want, 32) != 0) return fail(r, "422 Unprocessable Entity", "SHA-256 mismatch: not flashing");
  if (!Update.end(true)) return fail(r, "422 Unprocessable Entity", "the image didn't verify");

  JsonDocument d;
  d["ok"] = true;
  d["bytes"] = (unsigned)got;
  d["slot"] = next->label;
  d["note"] = "rebooting into the new image; it confirms itself after 45 s, or the previous one comes back";
  std::string s;
  serializeJson(d, s);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, s.data(), s.size());
  Serial.printf("ota: %u bytes written to %s, rebooting\n", (unsigned)got, next->label);
  xTaskCreate(rebootSoon, "reboot", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

}  // namespace lynx
