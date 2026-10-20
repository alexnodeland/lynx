<p align="center">
  <img src="media/readme/hero.png" alt="lynx — a pan/tilt camera for the Seeed XIAO ESP32-S3 Sense" width="100%">
</p>

<p align="center">
  <a href="https://github.com/alexnodeland/lynx/actions/workflows/pages.yml"><img alt="Pages deploy" src="https://img.shields.io/github/actions/workflow/status/alexnodeland/lynx/pages.yml?branch=main&label=site&logo=github"></a>
  <a href="https://alexnodeland.github.io/lynx/"><img alt="Site" src="https://img.shields.io/badge/site-alexnodeland.github.io%2Flynx-2ea44f"></a>
  <img alt="Printable parts" src="https://img.shields.io/badge/printed%20parts-8-555">
  <a href="firmware/lynx-motion"><img alt="PlatformIO" src="https://img.shields.io/badge/PlatformIO-ESP32--S3-f5822a?logo=platformio&logoColor=white"></a>
  <a href="web"><img alt="three.js" src="https://img.shields.io/badge/three.js-0.170-000?logo=threedotjs"></a>
  <br>
  <a href="LICENSES/CERN-OHL-S-2.0.txt"><img alt="Hardware licence" src="https://img.shields.io/badge/hardware-CERN--OHL--S--2.0-blue"></a>
  <a href="LICENSES/MIT.txt"><img alt="Software licence" src="https://img.shields.io/badge/firmware%20%2B%20web-MIT-green"></a>
  <a href="LICENSES/CC-BY-SA-4.0.txt"><img alt="Docs licence" src="https://img.shields.io/badge/docs-CC--BY--SA--4.0-lightgrey"></a>
</p>

# lynx

An open-source pan/tilt camera built around the Seeed XIAO ESP32-S3 Sense. It has eight 3D-printed parts and two MG90S servos, runs on one USB-C cable, and is held together by one screw type (M2 × 8 thread-forming).

**Site:** https://alexnodeland.github.io/lynx/

| Path | Contents |
|------|----------|
| [`hardware/production`](hardware/production) | Print-ready STL/STEP, assembly STEP, [BOM](hardware/production/BOM.md) |
| [`hardware/electronics`](hardware/electronics) | Circuit, nets, parts, wiring harness (servo-carrier PCB in progress) |
| [`firmware/lynx-motion`](firmware/lynx-motion) | PlatformIO motor-control firmware: pan D0, tilt D1, eased moves, serial + HTTP API |
| [`web`](web) | three.js landing page (Vite + GSAP) |
| `media` | Renders and screenshots |

## Specs

| | |
|---|---|
| Controller | Seeed XIAO ESP32-S3 Sense (camera, microSD, Wi-Fi) |
| Actuators | 2 × MG90S micro servo — pan on D0 (GPIO1), tilt on D1 (GPIO2), 220 Ω series resistors |
| Travel | pan ±90°, tilt ±60° mechanical; firmware soft limits pan ±88°, tilt ±58° |
| Motion | eased "glance" moves, default max speed 300 °/s, PWM detached when idle |
| Power + data | one USB-C cable via a panel socket in the base (Adafruit 6069); supply ≥ 1 A |
| Decoupling | 470 µF 6.3 V bulk + 100 nF across the servo 5 V rail |
| Fasteners | 9 × M2 × 8 thread-forming, pan head — the only screw |
| Printed parts | 8 (base, lid, battery tray, yoke, pan base, pan cap, servo shroud, light pipe) |
| Control | serial console (115200) and HTTP API / control page over Wi-Fi |
| Battery | optional 150 mAh LiPo keeps the camera up; servos run on USB power only |

## System

```mermaid
flowchart LR
  USB["USB-C supply (≥ 1 A)"] --> SOCK["Panel socket<br/>Adafruit 6069"]
  SOCK --> XIAO["XIAO ESP32-S3 Sense"]
  BAT["LiPo 150 mAh<br/>(optional)"] -.-> XIAO
  XIAO -- "5V / GND" --> RAIL["Servo rail<br/>470 µF + 100 nF"]
  XIAO -- "D0 → 220 Ω" --> PAN["Pan servo<br/>MG90S"]
  XIAO -- "D1 → 220 Ω" --> TILT["Tilt servo<br/>MG90S"]
  RAIL --> PAN
  RAIL --> TILT
  XIAO -. "Wi-Fi: HTTP API" .- CLIENT["Browser / curl"]
```

## Gallery

| | |
|---|---|
| ![Exploded view](media/readme/exploded.jpg) | ![Assembled](media/readme/assembled.jpg) |
| Exploded: eight parts, one screw type | Assembled on the pan base |
| ![USB-C socket](media/readme/usb-c.jpg) | ![microSD slot](media/readme/microsd.jpg) |
| One USB-C cable into the base | microSD slot above the USB-C port |
| ![Site hero](media/readme/site-hero.jpg) | ![Site motion section](media/readme/site-motion.jpg) |
| The landing page | Its motion section |

## Parts

Full list with suppliers in [`hardware/production/BOM.md`](hardware/production/BOM.md).

| Qty | Part |
|---|---|
| 8 | printed parts (`hardware/production/*.stl`) |
| 1 | Seeed XIAO ESP32-S3 Sense |
| 2 | MG90S micro servo (Waveshare 15024) or Adafruit SG92R |
| 1 | panel-mount USB-C extension, Adafruit 6069 |
| 1 | right-angle USB-C to USB-C, thin, data-capable, 15–20 cm |
| 1 | 470 µF 6.3 V electrolytic (Panasonic EEU-FR0J471) + 100 nF 0603 |
| 2 | 220 Ω resistor |
| 9 | M2 × 8 thread-forming screw, pan head |
| 1 | 12.7 × 3.17 mm N42 disc magnet (K&J D82) |
| — | optional: 150 mAh LiPo (Adafruit 1317), microSD card |

## Quick start

### 1. Print

Print the three `test-*.stl` fit coupons first, then the eight parts in [`hardware/production`](hardware/production). Orientation, supports and materials per part are in the [BOM](hardware/production/BOM.md#printed-parts); defaults are 0.2 mm layers and 3 walls.

### 2. Assemble

Follow the ten steps in the [BOM's assembly section](hardware/production/BOM.md#assembly). Wire the servos as in [`hardware/electronics`](hardware/electronics) — carrier PCB or soldered harness, same nets.

### 3. Flash

```sh
cd firmware/lynx-motion
pio run -e xiao -t upload       # build and flash over USB-C
pio device monitor -b 115200    # serial console: try `center`, `sweep`, `status`
```

Calibrate before fixing the horns — see [`firmware/lynx-motion`](firmware/lynx-motion#calibration).

### 4. Run the site locally

```sh
cd web
npm ci
npm run dev
```

## Licences

- Hardware: CERN-OHL-S-2.0
- Firmware and web: MIT
- Docs and media: CC-BY-SA-4.0

The licence texts are in [`LICENSES/`](LICENSES).
