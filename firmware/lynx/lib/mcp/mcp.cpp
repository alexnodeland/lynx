// SPDX-License-Identifier: MIT
#include "mcp.h"

#include <cmath>

namespace lynx {

static const char* kVersionKey = "io.modelcontextprotocol/protocolVersion";
static const char* kLegacy[] = {"2025-11-25", "2025-06-18", "2025-03-26"};
// Sent on initialize / server/discover. Kept short: what an agent needs before its first call.
static const char* kInstructions =
    "lynx is a pan/tilt camera on the user's network. You can aim it, look through it, and use its on-device "
    "detector. Call the guide tool (or read the lynx://guide resource) for the full manual.\n"
    "Angles are degrees. pan -88..88: 0 = straight ahead, positive = toward the camera's left. tilt -58..58: "
    "0 = lens straight up, 45 = the rest pose (leaning forward), 58 = looking down at the front. "
    "look returns the move's duration; wait that long before a snapshot.\n"
    "To see: snapshot returns a JPEG (annotate: true draws the current detections). Video is not carried over "
    "MCP; stream_url gives a live MJPEG URL for a person to open.\n"
    "To find things: detect returns faces, cat faces or a custom model's objects as boxes (x, y, w, h, 0..1, origin "
    "top-left). One frame is about 62 x 48 degrees: to centre a box, move pan by about (cx - 0.5) * 62 and tilt "
    "by about (cy - 0.5) * 48, then detect again; if it moved the wrong way, reverse that axis (it depends on "
    "how the board is mounted). follow does this continuously for the nearest face.\n"
    "Who moves the head: a command (yours, the dashboard's, Home Assistant's) wins for 5 s, then follow while it "
    "sees a face, then patrol. If the head drifts after you moved it, follow or patrol took over: turn them off.\n"
    "Privacy: detection, not recognition; nobody is identified. Describe what you see; don't guess who someone is. "
    "Ask before turning on recording or patrol, which keep running after the conversation.";

// The full manual: the guide tool and the lynx://guide resource.
static const char* kGuide =
    "# lynx: guide for agents\n\n"
    "lynx is an open-source pan/tilt camera (XIAO ESP32-S3 Sense, two servos). This MCP server runs on the "
    "camera itself. Everything here acts on the real device in the user's home.\n\n"
    "## Coordinates\n"
    "- pan -88..88 degrees. 0 is straight ahead; positive turns toward the camera's left (the viewer's right when "
    "facing it).\n"
    "- tilt -58..58 degrees. 0 points the lens straight up; 45 is the rest pose, leaning forward; 58 is the "
    "lowest, looking down at the front.\n"
    "- The field of view is about 62 x 48 degrees, so one frame-width is about 62 degrees of pan.\n"
    "- Moves are eased like a head turn. look returns its duration in ms: wait at least that long, plus ~300 ms "
    "for the image to settle, before snapshot.\n\n"
    "## Seeing\n"
    "- snapshot: a JPEG of what the camera sees now. annotate: true draws the current detections as green boxes.\n"
    "- stream_url: live MJPEG and still URLs for a person (browser, VLC, Home Assistant). They need the token.\n"
    "- set_camera: resolution (QVGA..UXGA), JPEG quality, flip, mirror, brightness. Higher resolutions make "
    "snapshots larger and slower; VGA is a good default.\n\n"
    "## Detecting and following\n"
    "- detect with no arguments: what is in view now. Boxes are normalised (x, y, w, h; origin top-left).\n"
    "- detect {enabled: true, model: face|cat|custom, range: near|far}: switch the detector. It stays on.\n"
    "  far finds faces down to ~12 % of the frame (across a room); near needs ~30 % but is faster. custom is an "
    "Edge Impulse model the owner installed; its labels are whatever they trained.\n"
    "- It runs about 4 times a second. A result can be up to ~250 ms old.\n"
    "- To centre an object at box centre (cx, cy): change pan by about (cx - 0.5) * 62 and tilt by about "
    "(cy - 0.5) * 48, then detect again. Which way is positive depends on how the board sits in the head (flip "
    "and mirror settings), so if the object moved further off-centre, reverse that axis.\n"
    "- follow {enabled: true}: the head keeps the nearest face centred, holds 6 s after losing it, then returns "
    "to rest. Detection must be on.\n\n"
    "## Presets and patrol\n"
    "- preset {action: list|go|save|delete, name}: up to 8 named positions (a-z, 0-9, - and _). save without "
    "pan/tilt stores where the head is now.\n"
    "- patrol {enabled, presets, dwell_s}: tour presets on a loop.\n"
    "- Priority: a command (from you, the dashboard, Home Assistant or the serial console) wins for 5 s; then "
    "follow, while it sees a face; then patrol.\n\n"
    "## Recordings (microSD)\n"
    "- recordings {action: status}: is there a card, how full, what is being recorded.\n"
    "- {action: configure, on_detect, timelapse_min}: save snapshots when something is detected, and/or a frame "
    "every N minutes. Off by default.\n"
    "- {action: list, path: /lynx} lists days; path: /lynx/YYYYMMDD lists that day's files; {action: get, path} "
    "returns one as an image. Times are UTC.\n\n"
    "## Good practice\n"
    "- Say what you are about to do before moving the camera if a person is watching.\n"
    "- Ask the user before turning on follow, patrol or recording: they keep running after you finish.\n"
    "- Detection finds a face; it doesn't know whose. Don't try to identify people.\n"
    "- When done, center returns the head to rest.\n\n"
    "## Recipes\n"
    "- Look around: for pan in -60, -20, 20, 60: look(pan, tilt 40), wait, snapshot; describe the room.\n"
    "- Find something: detect (custom model if it was trained for it) or look around and inspect snapshots; "
    "centre it with look; snapshot with annotate.\n"
    "- Check a place: preset go, wait, snapshot.\n";

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

enum Hint : unsigned { kReadOnly = 1, kIdempotent = 2, kDestructive = 4 };

static void addTool(JsonArray tools, const char* name, const char* title, const char* desc, unsigned hints,
                    JsonObject* propsOut = nullptr) {
  JsonObject t = tools.add<JsonObject>();
  t["name"] = name;
  t["title"] = title;
  t["description"] = desc;
  JsonObject s = t["inputSchema"].to<JsonObject>();
  s["type"] = "object";
  JsonObject props = s["properties"].to<JsonObject>();
  if (propsOut) *propsOut = props;
  t["annotations"]["title"] = title;
  t["annotations"]["readOnlyHint"] = (hints & kReadOnly) != 0;
  t["annotations"]["idempotentHint"] = (hints & (kReadOnly | kIdempotent)) != 0;
  t["annotations"]["destructiveHint"] = (hints & kDestructive) != 0;
  t["annotations"]["openWorldHint"] = false;
}

static void describe(JsonObject props, const char* name, const char* type, const char* desc) {
  props[name]["type"] = type;
  props[name]["description"] = desc;
}

static void listTools(JsonObject result) {
  JsonArray tools = result["tools"].to<JsonArray>();
  addTool(tools, "guide", "Guide",
          "The manual for this camera: coordinates, how to find and centre things, priorities, recipes. Read it "
          "before your first move if you haven't used lynx before.",
          kReadOnly);
  addTool(tools, "status", "Status",
          "Current pan/tilt angles, motion state, camera settings, detection, presets and network info.", kReadOnly);
  JsonObject props;
  addTool(tools, "look", "Look",
          "Move the head to an absolute pan/tilt position with an eased, natural motion. Omit pan or tilt to keep it. "
          "Returns how long the move takes: wait that long before a snapshot.",
          kIdempotent, &props);
  schemaNumber(props, "pan", -88, 88, "Pan angle in degrees; 0 is straight ahead, positive is the camera's left.");
  schemaNumber(props, "tilt", -58, 58, "Tilt angle in degrees; 0 is lens straight up, 45 is the rest pose.");
  schemaNumber(props, "ms", 0, 10000, "Optional move duration in milliseconds; omitted = chosen from distance.");
  addTool(tools, "center", "Center", "Return to the rest pose: pan 0, tilt 45.", kIdempotent);
  addTool(tools, "sweep", "Sweep", "Run a demo sweep through the full range of motion (about 8 s).", 0);
  addTool(tools, "snapshot", "Snapshot",
          "Take a JPEG photo with the camera and return it as an image. annotate: true draws the current detections "
          "(green boxes) on it.",
          kReadOnly, &props);
  describe(props, "annotate", "boolean", "Draw the current detections (green boxes) on the image.");
  addTool(tools, "stream_url", "Stream URL",
          "URLs for the live MJPEG video stream and still snapshots (open in a browser, VLC, ffmpeg or Home Assistant).",
          kReadOnly);
  addTool(tools, "set_camera", "Camera settings", "Change camera settings; omitted fields stay as they are.",
          kIdempotent, &props);
  JsonObject fs = props["framesize"].to<JsonObject>();
  fs["type"] = "string";
  JsonArray e = fs["enum"].to<JsonArray>();
  for (const char* v : {"QVGA", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA"}) e.add(v);
  fs["description"] = "Resolution: QVGA 320x240 .. UXGA 1600x1200.";
  schemaNumber(props, "quality", 4, 63, "JPEG quality, lower is better (4..63).");
  describe(props, "vflip", "boolean", "Flip the image upside down.");
  describe(props, "hmirror", "boolean", "Mirror the image left to right.");
  schemaNumber(props, "brightness", -2, 2, "Brightness -2..2.");
  addTool(tools, "detect", "Detect",
          "On-device detection: which faces, cat faces, or (with a custom model installed) other things the camera sees now, as boxes normalised to the frame "
          "(x, y, w, h; origin top-left). With no arguments it only reads. Pass enabled/model/range to switch it; it "
          "stays on until turned off.",
          kIdempotent, &props);
  describe(props, "enabled", "boolean", "Turn the detector on or off.");
  JsonObject dm = props["model"].to<JsonObject>();
  dm["type"] = "string";
  dm["enum"].add("face");
  dm["enum"].add("cat");
  dm["enum"].add("custom");
  dm["description"] = "face: human faces; cat: cat faces; custom: the Edge Impulse model the owner installed.";
  JsonObject dr = props["range"].to<JsonObject>();
  dr["type"] = "string";
  dr["enum"].add("near");
  dr["enum"].add("far");
  dr["description"] = "near: faces filling ~30 % of the frame (fastest); far: down to ~12 %, across a room.";
  addTool(tools, "follow", "Follow", "Turn follow mode on or off: the head keeps the nearest face in the middle of the frame.",
          kIdempotent, &props);
  describe(props, "enabled", "boolean", "true to follow (needs detection on), false to stop.");
  t_required(tools[tools.size() - 1], "enabled");
  addTool(tools, "preset", "Preset",
          "Named positions: list them, go to one, save the current (or a given) position under a name, or delete one. "
          "Up to 8.",
          kDestructive, &props);
  JsonObject pa = props["action"].to<JsonObject>();
  pa["type"] = "string";
  for (const char* v : {"list", "go", "save", "delete"}) pa["enum"].add(v);
  pa["description"] = "list the presets, go to one, save one, or delete one.";
  props["name"]["type"] = "string";
  props["name"]["description"] = "1-15 characters: a-z, 0-9, - and _";
  schemaNumber(props, "pan", -88, 88, "For save: the pan angle (default: where the head is now).");
  schemaNumber(props, "tilt", -58, 58, "For save: the tilt angle (default: where the head is now).");
  t_required(tools[tools.size() - 1], "action");
  addTool(tools, "patrol", "Patrol",
          "Tour presets on a loop, dwelling at each. Follow mode takes over while it sees a face; a manual move pauses it. "
          "It keeps running after you finish: ask the user first.",
          kIdempotent, &props);
  describe(props, "enabled", "boolean", "Start or stop the patrol.");
  JsonObject pp = props["presets"].to<JsonObject>();
  pp["type"] = "array";
  pp["items"]["type"] = "string";
  pp["description"] = "Preset names in visiting order (default: all)";
  schemaNumber(props, "dwell_s", 1, 3600, "Seconds at each stop (default 10).");
  t_required(tools[tools.size() - 1], "enabled");
  addTool(tools, "recordings", "Recordings",
          "The microSD card: status and recording settings (snapshots on detection, timelapse), list days or a day's "
          "files, or get one recording as an image. Recording is off until configured; ask the user before turning it on.",
          kIdempotent, &props);
  JsonObject ra = props["action"].to<JsonObject>();
  ra["type"] = "string";
  for (const char* v : {"status", "configure", "list", "get"}) ra["enum"].add(v);
  ra["description"] = "status: card and settings; configure: change settings; list: days or files; get: one image.";
  props["path"]["type"] = "string";
  props["path"]["description"] = "list: /lynx (days) or /lynx/YYYYMMDD; get: a .jpg path from list";
  describe(props, "on_detect", "boolean", "configure: save snapshots when something is detected.");
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
  if (name == "guide") {
    text(content, kGuide);
  } else if (name == "status") {
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

// ---------- resources ----------

static void listResources(JsonObject result) {
  JsonArray r = result["resources"].to<JsonArray>();
  JsonObject g = r.add<JsonObject>();
  g["uri"] = "lynx://guide";
  g["name"] = "guide";
  g["title"] = "lynx guide for agents";
  g["description"] = "Coordinates, finding and centring things, priorities, recordings, good practice, recipes.";
  g["mimeType"] = "text/markdown";
  JsonObject st = r.add<JsonObject>();
  st["uri"] = "lynx://status";
  st["name"] = "status";
  st["title"] = "Live status";
  st["description"] = "The camera's current state as JSON (the same as the status tool).";
  st["mimeType"] = "application/json";
}

static bool readResource(Device& dev, const std::string& uri, JsonObject result) {
  JsonObject c = result["contents"].to<JsonArray>().add<JsonObject>();
  c["uri"] = uri;
  if (uri == "lynx://guide") {
    c["mimeType"] = "text/markdown";
    c["text"] = kGuide;
  } else if (uri == "lynx://status") {
    JsonDocument st;
    dev.status(st.to<JsonObject>());
    c["mimeType"] = "application/json";
    c["text"] = dump(st);
  } else {
    return false;
  }
  return true;
}

// ---------- prompts ----------

struct PromptDef {
  const char* name;
  const char* title;
  const char* description;
  const char* arg;      // one optional/required argument, or nullptr
  const char* argDesc;
  bool argRequired;
  const char* text;     // "%s" is replaced by the argument
};

static const PromptDef kPrompts[] = {
    {"look_around", "Look around", "Scan the room in a few steps and describe what the camera can see.", nullptr,
     nullptr, false,
     "Use the lynx camera to look around the room. Look at pan -60, -20, 20 and 60 (tilt 40), wait for each move, "
     "take a snapshot at each, then return to center. Describe the room as a whole: layout, notable objects, "
     "people (without guessing who they are), and anything that looks out of place."},
    {"find", "Find something", "Search the room for a thing and point the camera at it.", "thing",
     "What to look for, e.g. \"my keys\" or \"the cat\"", true,
     "Use the lynx camera to find %s. If detection has a model for it (faces, cat faces, or a custom model), use "
     "detect; otherwise look around in steps and inspect snapshots. When you find it, centre it with look and take "
     "an annotated snapshot. Say where it is in the room. If you can't find it, say where you looked."},
    {"check", "Check on a place", "Go to a saved preset (or a described spot) and report what's there.", "place",
     "A preset name, or a description like \"the front door\"", true,
     "Use the lynx camera to check on %s. If it is a preset name (preset list), go there; otherwise find the spot "
     "by looking around. Wait for the move, take a snapshot, and report what you see in two or three sentences. "
     "Then return to center."},
};

static void listPrompts(JsonObject result) {
  JsonArray ps = result["prompts"].to<JsonArray>();
  for (const PromptDef& d : kPrompts) {
    JsonObject p = ps.add<JsonObject>();
    p["name"] = d.name;
    p["title"] = d.title;
    p["description"] = d.description;
    JsonArray args = p["arguments"].to<JsonArray>();
    if (d.arg) {
      JsonObject a = args.add<JsonObject>();
      a["name"] = d.arg;
      a["description"] = d.argDesc;
      a["required"] = d.argRequired;
    }
  }
}

static bool getPrompt(const std::string& name, JsonObjectConst args, JsonObject result, std::string& err) {
  for (const PromptDef& d : kPrompts) {
    if (name != d.name) continue;
    std::string body = d.text;
    if (d.arg) {
      std::string v = args[d.arg] | "";
      if (v.empty() && d.argRequired) {
        err = std::string("prompt ") + d.name + " needs argument " + d.arg;
        return false;
      }
      size_t at = body.find("%s");
      if (at != std::string::npos) body.replace(at, 2, v);
    }
    result["description"] = d.description;
    JsonObject m = result["messages"].to<JsonArray>().add<JsonObject>();
    m["role"] = "user";
    m["content"]["type"] = "text";
    m["content"]["text"] = body;
    return true;
  }
  err = "unknown prompt: " + name;
  return false;
}

static void capabilities(JsonObject result) {
  result["capabilities"]["tools"]["listChanged"] = false;
  result["capabilities"]["resources"]["listChanged"] = false;
  result["capabilities"]["resources"]["subscribe"] = false;
  result["capabilities"]["prompts"]["listChanged"] = false;
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
    capabilities(result);
    result["serverInfo"]["name"] = dev.name();
    result["serverInfo"]["title"] = "lynx pan/tilt camera";
    result["serverInfo"]["version"] = dev.version();
    result["instructions"] = kInstructions;
  } else if (method == "server/discover") {
    JsonArray s = result["supportedVersions"].to<JsonArray>();
    s.add(kModernVersion);
    for (const char* l : kLegacy) s.add(l);
    capabilities(result);
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
  } else if (method == "resources/list") {
    listResources(result);
  } else if (method == "resources/templates/list") {
    result["resourceTemplates"].to<JsonArray>();
  } else if (method == "resources/read") {
    std::string uri = in["params"]["uri"] | "";
    if (!readResource(dev, uri, result)) return error(200, id, -32002, "Resource not found: " + uri);
  } else if (method == "prompts/list") {
    listPrompts(result);
  } else if (method == "prompts/get") {
    std::string err;
    if (!getPrompt(in["params"]["name"] | "", in["params"]["arguments"].as<JsonObjectConst>(), result, err))
      return error(200, id, kInvalidParams, err);
  } else {
    return error(modern ? 404 : 200, id, kMethodNotFound, "Method not found: " + method);
  }
  return {200, dump(out)};
}

}  // namespace lynx
