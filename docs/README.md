# lynx documentation

Pick the path that matches what you're doing. Every page links back here.

## I want to build one

| | |
|---|---|
| **[Interactive build guide](https://alexnodeland.github.io/lynx/build.html)** | 13 steps in 3D, from printed parts to a camera on your network. Start here. |
| [Bill of materials](../hardware/production/BOM.md) | every part with a supplier and part number, plus the print settings |
| [STL / STEP files](../hardware/production) | eight print-ready parts, three test coupons, the full assembly |
| [Electronics](../hardware/electronics) | circuit, nets, the carrier board and the wiring harness |
| [Web flasher](https://alexnodeland.github.io/lynx/flash.html) | install the firmware from Chrome or Edge, no toolchain |
| [Troubleshooting](troubleshooting.md) | what's gone wrong before, and the fix |
| [Custom detection models](custom-models.md) | teach it new things: capture with lynx, train in Edge Impulse, `just model-add` |

## I have one and want to use it

| | |
|---|---|
| [Setup and the dashboard](../firmware/lynx/README.md#setup-first-boot) | the setup hotspot, the token, `lynx-xxxx.local` |
| [HTTP API](../firmware/lynx/README.md#http-api) | position, rest pose, sweep, camera settings, snapshot, MJPEG stream |
| [MCP for AI agents](../firmware/lynx/README.md#mcp) | tools, both protocol generations, `claude mcp add …` |
| [Home Assistant](../custom_components/lynx/README.md) | HACS install, entities, the `lynx.look` action, automations |
| [Security model](../firmware/lynx/README.md#security-model) | the token, where it's shown, what's open, why no TLS |
| [Calibration](../firmware/lynx/README.md#calibration) | trims, direction, limits, from the serial console |

## I want to change it

| | |
|---|---|
| [Contributing](../CONTRIBUTING.md) | setup in one command, the gates, conventions, how a release works |
| [Architecture](architecture.md) | how the hardware, firmware, integration and site fit together, with diagrams |
| [Firmware internals](../firmware/lynx/README.md#layout) | what lives in `lib/` (tested natively) vs `src/` (hardware glue) |
| [Web](../web/README.md) | the landing page, the build guide and the flasher, on one shared three.js scene |

## Licences

Hardware (CAD, STL/STEP, electronics) is **CERN-OHL-S-2.0**. Firmware, web and the Home Assistant
integration are **MIT**. Documentation and media are **CC-BY-SA-4.0**. The texts are in [`LICENSES/`](../LICENSES),
and every source file carries an `SPDX-License-Identifier`.
