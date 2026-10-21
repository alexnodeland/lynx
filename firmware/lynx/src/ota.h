// SPDX-License-Identifier: MIT
// Over-the-air updates with automatic rollback.
//
// POST /api/update takes an app image (the build's firmware.bin, not the merged lynx.bin the web
// flasher uses), writes it to the other OTA slot and reboots into it. The bootloader runs a new image
// "pending verification": lynx marks it good only after it has run healthily for a while
// (kHealthyAfterMs, serving HTTP). If it crashes, hangs (the watchdog resets it) or reboots before
// that, the bootloader goes back to the previous image by itself.
#pragma once
#include <ArduinoJson.h>
#include <esp_http_server.h>

namespace lynx {

void otaBegin();                 // after the HTTP server is up
void otaLoop();                  // marks a pending image valid once it has proved itself
bool otaInProgress();            // detection and the stream producer stand down meanwhile
void otaJson(JsonObject out);    // version, running slot, pending state
esp_err_t otaUpload(httpd_req_t* r);  // the POST /api/update body handler (already authorised)

}  // namespace lynx
