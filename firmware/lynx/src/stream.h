// SPDX-License-Identifier: MIT
// Port 81: the live video and the push events, on a small server of its own.
//   GET /stream[?fps=N]   MJPEG, several viewers at once (up to 4 connections in all)
//   GET /events           server-sent events: the status, pushed when something changes
// One producer task takes each camera frame once and shares it; every client gets its own task, so a
// slow viewer only ever slows itself. Both endpoints need the device token (Bearer, or ?token=).
#pragma once
#include <cstdint>

namespace lynx {

void streamBegin();
int streamClients();  // open /stream + /events connections
uint32_t streamFrameSeq();  // frames shared so far

// Something changed that /events subscribers should hear about now (moves, detections, settings).
void eventsPoke();

}  // namespace lynx
