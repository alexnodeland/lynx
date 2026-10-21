// SPDX-License-Identifier: MIT
// microSD recording: snapshots when something is detected, an optional timelapse, and retention so the
// card never fills (lib/recorder decides; this carries it out). Files: /lynx/YYYYMMDD/HHMMSS_kind.jpg
// (the clock comes from NTP once lynx is on Wi-Fi). Off by default: nothing is recorded until you ask.
#pragma once
#include <ArduinoJson.h>
#include <esp_http_server.h>

#include <string>

namespace lynx {

void sdBegin();
bool sdMounted();
void sdJson(JsonObject out);                                // card, usage, recording settings, counts
bool applySdJson(JsonObjectConst in, std::string& error);  // {onDetect, timelapseMin}
bool sdList(const char* dir, JsonArray out, std::string& error);  // dir "/lynx" lists days; a day lists files
esp_err_t sdSendFile(httpd_req_t* r, const char* path);     // GET a recording (JPEG)
bool sdDelete(const char* path, std::string& error);        // a file, or a whole day
bool sdRead(const char* path, std::string& jpeg, std::string& error);  // for MCP

}  // namespace lynx
