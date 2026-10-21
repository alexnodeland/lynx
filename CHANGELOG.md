# Changelog

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
