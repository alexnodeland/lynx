// SPDX-License-Identifier: MIT
// HTTP: port 80 = pages, REST API, MCP, snapshots; port 81 = MJPEG stream (its own task, so a
// viewer can never block control).
#pragma once

namespace lynx {

void webBegin();

}  // namespace lynx
