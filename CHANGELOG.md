# Changelog

## Unreleased

- **On-device detection:** faces or cat faces (ESP-DL), with `near`/`far` range. It runs at ~4 fps on a scaled copy
  of each frame, so the stream keeps its 20 fps. Exposed on REST (`/api/detections`, `/api/detect`), MCP (`detect`),
  Home Assistant (occupancy and count sensors, detection switch, model and range selects), and the dashboard (live boxes).
- **Follow mode:** keeps the nearest face centred, using no-overshoot track moves. It holds, then returns to rest.
  Manual moves pause it. `lib/follow` has native tests, including a closed loop.
- `/api/detect/image` runs detection on an uploaded JPEG (`?debug=1` returns the model's input).
- **Custom models (Edge Impulse):** `just capture <label>` collects training images with lynx itself, and
  `just model-add export.zip` installs an export as `model: custom` (FOMO boxes or a classifier's label). Proven on
  the board with a public export; CI builds with it.
- Fix: the demo sweep ends at the rest pose instead of lens-up.

## 0.3.0 (2026-10-20)

First public release.

- **Hardware:** eight printed parts, one screw type (9 × M2 × 8), CAD-validated pan ±90° / tilt ±60°
  (clear to ±90° tilt), magnet mount, a single USB-C cable through a panel socket.
- **Firmware:** eased pan/tilt motion with a 45° rest pose; camera snapshots and an MJPEG stream; a REST API;
  an on-device MCP server (2026-07-28 and 2025-era clients); a per-device token; a first-boot setup
  hotspot with captive portal; mDNS with IPv6. 39 native tests.
- **Home Assistant:** an integration (HACS) with zeroconf discovery, camera + live view, pan/tilt,
  camera settings, diagnostics and a `lynx.look` action. 20 tests, hassfest, HACS validation.
- **Website:** an immersive landing page, a 13-step 3D build guide, and a browser flasher.
