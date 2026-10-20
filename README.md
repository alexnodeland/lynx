# lynx

An open-source pan/tilt camera built around the Seeed XIAO ESP32-S3 Sense. It has eight 3D-printed parts and two MG90S servos, runs on one USB-C cable, and is held together by one screw type (M2 × 8 thread-forming).

**Site:** https://alexnodeland.github.io/lynx/

| Path | Contents |
|------|----------|
| `hardware/production` | Print-ready STL/STEP, assembly STEP, BOM |
| `hardware/electronics` | Circuit, nets, parts, wiring harness (servo-carrier PCB in progress) |
| `firmware/lynx-motion` | PlatformIO motor-control firmware: pan D0, tilt D1, eased moves, serial + HTTP API |
| `web` | three.js landing page (Vite + GSAP) |
| `media` | Renders and screenshots |

## Licences

- Hardware: CERN-OHL-S-2.0
- Firmware and web: MIT
- Docs and media: CC-BY-SA-4.0

The licence texts are in `LICENSES/`.
