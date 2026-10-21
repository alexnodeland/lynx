// SPDX-License-Identifier: MIT
#include "mcp.h"

#include <cmath>

namespace lynx {

static const char* kVersionKey = "io.modelcontextprotocol/protocolVersion";
static const char* kLegacy[] = {"2025-11-25", "2025-06-18", "2025-03-26"};
static const char* kInstructions =
    "lynx is a pan/tilt camera. Angles are degrees: pan -88..88 (0 = straight ahead, positive = to the "
    "camera's left), tilt -58..58 (0 = lens straight up, 45 = the rest pose, leaning toward the front). "
    "Use snapshot to see what it sees; video is not carried over MCP, use stream_url for a live MJPEG feed. "
    "detect runs on-device face (or cat-face) detection and returns boxes; follow keeps a face in frame. "
    "Detection only: nobody is identified.";

// JSON-RPC / MCP error codes
enum : int {
  kParseError = -32700,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kInvalidParams = -32602,
  kHeaderMismatch = -32020,
  kUnsupportedVersion = -32022,
};

bool isLegacyVersion(const std::string& v) {
  for (const char* l : kLegacy)
    if (v == l) return true;
  return false;
}

std::string base64(const uint8_t* d, size_t n) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((n + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 2 < n; i += 3) {
    uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
    out += t[v >> 18];
    out += t[(v >> 12) & 63];
    out += t[(v >> 6) & 63];
    out += t[v & 63];
  }
  if (i < n) {
    uint32_t v = d[i] << 16;
    if (i + 1 < n) v |= d[i + 1] << 8;
    out += t[v >> 18];
    out += t[(v >> 12) & 63];
    out += i + 1 < n ? t[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

static std::string dump(const JsonDocument& doc) {
  std::string s;
  serializeJson(doc, s);
  return s;
}

static McpResponse error(int http, JsonVariantConst id, int code, const std::string& msg,
                         JsonDocument* data = nullptr) {
  JsonDocument d;
  d["jsonrpc"] = "2.0";
  if (id.isNull())
    d["id"] = nullptr;
  else
    d["id"] = id;
  d["error"]["code"] = code;
  d["error"]["message"] = msg;
  if (data) d["error"]["data"] = data->as<JsonVariantConst>();
  return {http, dump(d)};
}

static McpResponse unsupported(JsonVariantConst id, const std::string& requested) {
  JsonDocument data;
  JsonArray s = data["supported"].to<JsonArray>();
  s.add(kModernVersion);
  for (const char* l : kLegacy) s.add(l);
  data["requested"] = requested;
  return error(400, id, kUnsupportedVersion, "Unsupported protocol version", &data);
}

// ---------- tools ----------

static void schemaNumber(JsonObject props, const char* name, float lo, float hi, const char* desc) {
  JsonObject p = props[name].to<JsonObject>();
  p["type"] = "number";
  p["minimum"] = lo;
  p["maximum"] = hi;
  p["description"] = desc;
}

static void t_required(JsonObject tool, const char* name) { tool["inputSchema"]["required"].add(name); }

static void addTool(JsonArray tools, const char* name, const char* title, const char* desc, bool readOnly,
                    JsonObject* propsOut = nullptr) {
  JsonObject t = tools.add<JsonObject>();
  t["name"] = name;
  t["title"] = title;
  t["description"] = desc;
  JsonObject s = t["inputSchema"].to<JsonObject>();
  s["type"] = "object";
  JsonObject props = s["properties"].to<JsonObject>();
  if (propsOut) *propsOut = props;
  t["annotations"]["readOnlyHint"] = readOnly;
  t["annotations"]["destructiveHint"] = false;
  t["annotations"]["openWorldHint"] = false;
}

static void listTools(JsonObject result) {
  JsonArray tools = result["tools"].to<JsonArray>();
  addTool(tools, "status", "Status", "Current pan/tilt angles, motion state, camera settings and network info.", true);
  JsonObject props;
  addTool(tools, "look", "Look", "Move the head to an absolute pan/tilt position with an eased, natural motion.", false,
          &props);
  schemaNumber(props, "pan", -88, 88, "Pan angle in degrees; 0 is straight ahead.");
  schemaNumber(props, "tilt", -58, 58, "Tilt angle in degrees; 0 is lens straight up, 45 is the rest pose.");
  schemaNumber(props, "ms", 0, 10000, "Optional move duration in milliseconds; omitted = chosen from distance.");
  addTool(tools, "center", "Center", "Return to the rest pose: pan 0, tilt 45.", false);
  addTool(tools, "sweep", "Sweep", "Run a demo sweep through the full range of motion.", false);
  addTool(tools, "snapshot", "Snapshot",
          "Take a JPEG photo with the camera and return it as an image. annotate: true draws the current detections "
          "(green boxes) on it.",
          true, &props);
  props["annotate"]["type"] = "boolean";
  addTool(tools, "stream_url", "Stream URL",
          "URLs for the live MJPEG video stream and still snapshots (open in a browser, VLC, ffmpeg or Home Assistant).",
          true);
  addTool(tools, "set_camera", "Camera settings", "Change camera settings; omitted fields stay as they are.", false,
          &props);
  JsonObject fs = props["framesize"].to<JsonObject>();
  fs["type"] = "string";
  JsonArray e = fs["enum"].to<JsonArray>();
  for (const char* v : {"QVGA", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA"}) e.add(v);
  fs["description"] = "Resolution: QVGA 320x240 .. UXGA 1600x1200.";
  schemaNumber(props, "quality", 4, 63, "JPEG quality, lower is better (4..63).");
  props["vflip"]["type"] = "boolean";
  props["hmirror"]["type"] = "boolean";
  schemaNumber(props, "brightness", -2, 2, "Brightness -2..2.");
  addTool(tools, "detect", "Detect",
          "On-device detection: which faces, cat faces, or (with a custom model installed) other things the camera sees now, as boxes normalised to the frame "
          "(x, y, w, h; origin top-left). Pass enabled/model to switch it; it stays on until turned off.",
          false, &props);
  props["enabled"]["type"] = "boolean";
  JsonObject dm = props["model"].to<JsonObject>();
  dm["type"] = "string";
  dm["enum"].add("face");
  dm["enum"].add("cat");
  dm["enum"].add("custom");
  JsonObject dr = props["range"].to<JsonObject>();
  dr["type"] = "string";
  dr["enum"].add("near");
  dr["enum"].add("far");
  dr["description"] = "near: faces filling ~30 % of the frame (fastest); far: down to ~12 %, across a room.";
  addTool(tools, "follow", "Follow", "Turn follow mode on or off: the head keeps the nearest face in the middle of the frame.",
          false, &props);
  props["enabled"]["type"] = "boolean";
  t_required(tools[tools.size() - 1], "enabled");
  addTool(tools, "preset", "Preset",
          "Named positions: list them, go to one, save the current (or a given) position under a name, or delete one.",
          false, &props);
  JsonObject pa = props["action"].to<JsonObject>();
  pa["type"] = "string";
  for (const char* v : {"list", "go", "save", "delete"}) pa["enum"].add(v);
  props["name"]["type"] = "string";
  props["name"]["description"] = "1-15 characters: a-z, 0-9, - and _";
  schemaNumber(props, "pan", -88, 88, "For save: the pan angle (default: where the head is now).");
  schemaNumber(props, "tilt", -58, 58, "For save: the tilt angle (default: where the head is now).");
  t_required(tools[tools.size() - 1], "action");
  addTool(tools, "patrol", "Patrol",
          "Tour presets on a loop, dwelling at each. Follow mode takes over while it sees a face; a manual move pauses it.",
          false, &props);
  props["enabled"]["type"] = "boolean";
  JsonObject pp = props["presets"].to<JsonObject>();
  pp["type"] = "array";
  pp["items"]["type"] = "string";
  pp["description"] = "Preset names in visiting order (default: all)";
  schemaNumber(props, "dwell_s", 1, 3600, "Seconds at each stop (default 10).");
  t_required(tools[tools.size() - 1], "enabled");
  addTool(tools, "recordings", "Recordings",
          "The microSD card: status and recording settings (snapshots on detection, timelapse), list days or a day's "
          "files, or get one recording as an image.",
          false, &props);
  JsonObject ra = props["action"].to<JsonObject>();
  ra["type"] = "string";
  for (const char* v : {"status", "configure", "list", "get"}) ra["enum"].add(v);
  props["path"]["type"] = "string";
  props["path"]["description"] = "list: /lynx (days) or /lynx/YYYYMMDD; get: a .jpg path from list";
  props["on_detect"]["type"] = "boolean";
  schemaNumber(props, "timelapse_min", 0, 1440, "configure: minutes between timelapse frames, 0 = off");
  t_required(tools[tools.size() - 1], "action");
}

static void text(JsonArray content, const std::string& s) {
  JsonObject c = content.add<JsonObject>();
  c["type"] = "text";
  c["text"] = s;
}

// Fills `result` for tools/call; returns false (with message) for a JSON-RPC -32602 error.
static bool callTool(Device& dev, const std::string& name, JsonObjectConst args, JsonObject result, std::string& err) {
  JsonArray content = result["content"].to<JsonArray>();
  result["isError"] = false;
  char buf[160];
  if (name == "status") {
    JsonDocument st;
    dev.status(st.to<JsonObject>());
    result["structuredContent"] = st.as<JsonVariantConst>();
    text(content, dump(st));
  } else if (name == "look") {
    JsonVariantConst p = args["pan"], t = args["tilt"];
    if (p.isNull() && t.isNull()) {
      err = "look needs pan and/or tilt";
      return false;
    }
    JsonDocument st;
    dev.status(st.to<JsonObject>());
    float pan = p.isNull() ? st["pan"]["target"].as<float>() : p.as<float>();
    float tilt = t.isNull() ? st["tilt"]["target"].as<float>() : t.as<float>();
    if (!std::isfinite(pan) || !std::isfinite(tilt)) {
      err = "pan and tilt must be numbers";
      return false;
    }
    int32_t ms = args["ms"].isNull() ? -1 : args["ms"].as<int32_t>();
    uint32_t d = dev.look(pan, tilt, ms);
    snprintf(buf, sizeof buf, "moving to pan %.1f, tilt %.1f over %u ms (angles are clamped to the soft limits)", pan,
             tilt, (unsigned)d);
    text(content, buf);
  } else if (name == "center") {
    dev.center();
    text(content, "returning to rest: pan 0, tilt 45");
  } else if (name == "sweep") {
    dev.sweep();
    text(content, "sweeping the full range of motion");
  } else if (name == "snapshot") {
    std::string jpeg, e;
    if (!dev.snapshot(jpeg, e, args["annotate"] | false)) {
      result["isError"] = true;
      text(content, "snapshot failed: " + e);
      return true;
    }
    JsonObject img = content.add<JsonObject>();
    img["type"] = "image";
    img["data"] = base64((const uint8_t*)jpeg.data(), jpeg.size());
    img["mimeType"] = "image/jpeg";
  } else if (name == "stream_url") {
    text(content, "MJPEG stream: " + dev.streamUrl() + "\nSnapshot: " + dev.snapshotUrl() +
                      "\nBoth need the device token, as ?token=... or an Authorization: Bearer header.");
    result["structuredContent"]["stream"] = dev.streamUrl();
    result["structuredContent"]["snapshot"] = dev.snapshotUrl();
  } else if (name == "set_camera") {
    std::string e;
    if (!dev.setCamera(args, e)) {
      err = e;
      return false;
    }
    text(content, "camera settings updated");
  } else if (name == "detect") {
    if (!args["enabled"].isNull() || !args["model"].isNull()) {
      std::string e;
      if (!dev.setDetect(args, e)) {
        err = e;
        return false;
      }
    }
    JsonDocument d;
    dev.detections(d.to<JsonObject>());
    result["structuredContent"] = d.as<JsonVariantConst>();
    JsonArrayConst objs = d["objects"];
    if (!d["enabled"].as<bool>()) {
      text(content, "detection is off; call detect with enabled: true to start it");
    } else {
      snprintf(buf, sizeof buf, "%u %s%s in view", (unsigned)objs.size(), d["model"].as<const char*>(),
               objs.size() == 1 ? "" : "s");
      std::string s = buf;
      for (JsonObjectConst o : objs) {
        JsonArrayConst b = o["box"];
        float cx = b[0].as<float>() + b[2].as<float>() / 2, cy = b[1].as<float>() + b[3].as<float>() / 2;
        snprintf(buf, sizeof buf, "\n- %s %.0f%% at %s%s (box %.2f, %.2f, %.2f, %.2f)", o["label"].as<const char*>(),
                 o["score"].as<float>() * 100, cy < 0.33f ? "top " : (cy > 0.66f ? "bottom " : ""),
                 cx < 0.33f ? "left" : (cx > 0.66f ? "right" : "centre"), b[0].as<float>(), b[1].as<float>(),
                 b[2].as<float>(), b[3].as<float>());
        s += buf;
      }
      text(content, s);
    }
  } else if (name == "preset" || name == "patrol") {
    std::string e;
    bool ok = true;
    std::string action = args["action"] | "";
    if (name == "patrol") {
      JsonDocument a;
      a["enabled"] = args["enabled"];
      if (!args["presets"].isNull()) a["presets"] = args["presets"];
      if (!args["dwell_s"].isNull()) a["dwellS"] = args["dwell_s"];
      ok = dev.setPatrol(a.as<JsonObjectConst>(), e);
    } else if (action != "list") {
      ok = dev.preset(args, e);
    }
    if (!ok) {
      err = e;
      return false;
    }
    JsonDocument d;
    dev.presets(d.to<JsonObject>());
    result["structuredContent"] = d.as<JsonVariantConst>();
    std::string s;
    JsonArrayConst ps = d["presets"];
    if (name == "patrol") s = d["patrol"]["enabled"].as<bool>() ? "patrolling" : "patrol off";
    else if (action == "go") s = "going to " + std::string(args["name"] | "");
    else if (action == "save") s = "saved " + std::string(args["name"] | "");
    else if (action == "delete") s = "deleted " + std::string(args["name"] | "");
    else s = std::to_string(ps.size()) + " presets";
    for (JsonObjectConst p : ps) {
      snprintf(buf, sizeof buf, "\n- %s: pan %.0f, tilt %.0f", p["name"].as<const char*>(), p["pan"].as<float>(),
               p["tilt"].as<float>());
      s += buf;
    }
    text(content, s);
  } else if (name == "recordings") {
    std::string action = args["action"] | "status", e;
    if (action == "configure") {
      JsonDocument a;
      if (!args["on_detect"].isNull()) a["onDetect"] = args["on_detect"];
      if (!args["timelapse_min"].isNull()) a["timelapseMin"] = args["timelapse_min"];
      if (!dev.setRecording(a.as<JsonObjectConst>(), e)) {
        result["isError"] = true;
        text(content, e);
        return true;
      }
      action = "status";
    }
    if (action == "status") {
      JsonDocument d;
      dev.recordings(d.to<JsonObject>());
      result["structuredContent"] = d.as<JsonVariantConst>();
      text(content, dump(d));
    } else if (action == "list") {
      JsonDocument d;
      std::string dir = args["path"] | "/lynx";
      if (!dev.listRecordings(dir.c_str(), d["items"].to<JsonArray>(), e)) {
        result["isError"] = true;
        text(content, e);
        return true;
      }
      result["structuredContent"] = d.as<JsonVariantConst>();
      text(content, dump(d));
    } else if (action == "get") {
      std::string jpeg;
      if (!dev.readRecording(args["path"] | "", jpeg, e)) {
        result["isError"] = true;
        text(content, e);
        return true;
      }
      JsonObject img = content.add<JsonObject>();
      img["type"] = "image";
      img["data"] = base64((const uint8_t*)jpeg.data(), jpeg.size());
      img["mimeType"] = "image/jpeg";
    } else {
      err = "action must be status, configure, list or get";
      return false;
    }
  } else if (name == "follow") {
    if (args["enabled"].isNull()) {
      err = "follow needs enabled: true or false";
      return false;
    }
    std::string e;
    if (!dev.setFollow(args, e)) {
      err = e;
      return false;
    }
    text(content, args["enabled"].as<bool>() ? "following the nearest face" : "follow off");
  } else {
    err = "unknown tool: " + name;
    return false;
  }
  return true;
}

// ---------- dispatch ----------

McpResponse handleMcp(Device& dev, const McpRequest& req) {
  JsonDocument in;
  if (deserializeJson(in, req.body)) return error(400, JsonVariantConst(), kParseError, "Parse error");
  if (in.is<JsonArray>()) return error(400, JsonVariantConst(), kInvalidRequest, "Batch requests are not supported");
  JsonVariantConst id = in["id"];
  const char* methodC = in["method"];
  if (!methodC || in["jsonrpc"] != "2.0") return error(400, id, kInvalidRequest, "Invalid Request");
  std::string method = methodC;
  bool isNotification = !in.containsKey("id");

  JsonVariantConst metaVersion = in["params"]["_meta"][kVersionKey];
  bool modern = !metaVersion.isNull();
  std::string version;

  if (modern) {
    version = metaVersion.as<std::string>();
    if (version != kModernVersion) return unsupported(id, version);
    if (!req.protocolHeader.empty() && req.protocolHeader != version)
      return error(400, id, kHeaderMismatch,
                   "Header mismatch: MCP-Protocol-Version header value '" + req.protocolHeader +
                       "' does not match body value '" + version + "'");
    if (!req.methodHeader.empty() && req.methodHeader != method)
      return error(400, id, kHeaderMismatch,
                   "Header mismatch: Mcp-Method header value '" + req.methodHeader + "' does not match body value '" +
                       method + "'");
    if (method == "tools/call" && !req.nameHeader.empty()) {
      std::string bodyName = in["params"]["name"] | "";
      if (req.nameHeader != bodyName)
        return error(400, id, kHeaderMismatch,
                     "Header mismatch: Mcp-Name header value '" + req.nameHeader + "' does not match body value '" +
                         bodyName + "'");
    }
  } else if (method != "initialize" && !req.protocolHeader.empty()) {
    // a legacy client names its negotiated version in the header; a modern one must put it in _meta too
    if (req.protocolHeader == kModernVersion)
      return error(400, id, kHeaderMismatch,
                   "Header mismatch: MCP-Protocol-Version is 2026-07-28 but params._meta has no protocolVersion");
    if (!isLegacyVersion(req.protocolHeader)) return unsupported(id, req.protocolHeader);
  }

  if (isNotification) return {202, ""};  // notifications/initialized, cancelled, ...

  JsonDocument out;
  out["jsonrpc"] = "2.0";
  out["id"] = id;
  JsonObject result = out["result"].to<JsonObject>();
  if (modern) result["resultType"] = "complete";

  if (method == "initialize" && !modern) {
    std::string asked = in["params"]["protocolVersion"] | "";
    result["protocolVersion"] = isLegacyVersion(asked) ? asked : std::string(kLegacy[0]);
    result["capabilities"]["tools"]["listChanged"] = false;
    result["serverInfo"]["name"] = dev.name();
    result["serverInfo"]["title"] = "lynx pan/tilt camera";
    result["serverInfo"]["version"] = dev.version();
    result["instructions"] = kInstructions;
  } else if (method == "server/discover") {
    JsonArray s = result["supportedVersions"].to<JsonArray>();
    s.add(kModernVersion);
    for (const char* l : kLegacy) s.add(l);
    result["capabilities"]["tools"]["listChanged"] = false;
    result["instructions"] = kInstructions;
    JsonObject info = result["_meta"]["io.modelcontextprotocol/serverInfo"].to<JsonObject>();
    info["name"] = dev.name();
    info["version"] = dev.version();
  } else if (method == "ping") {
    // empty result
  } else if (method == "tools/list") {
    listTools(result);
  } else if (method == "tools/call") {
    const char* name = in["params"]["name"];
    if (!name) return error(200, id, kInvalidParams, "tools/call needs params.name");
    std::string err;
    if (!callTool(dev, name, in["params"]["arguments"].as<JsonObjectConst>(), result, err))
      return error(200, id, kInvalidParams, err);
  } else {
    return error(modern ? 404 : 200, id, kMethodNotFound, "Method not found: " + method);
  }
  return {200, dump(out)};
}

}  // namespace lynx
