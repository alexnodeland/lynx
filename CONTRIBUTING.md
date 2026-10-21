# Contributing to lynx

Thanks for building on lynx. Hardware tweaks, firmware features, integration fixes and build reports
are all welcome. This page gets you from clone to a green check in a few minutes.

## Setup (once)

```sh
git clone https://github.com/alexnodeland/lynx && cd lynx
just setup     # .venv with PlatformIO + the Home Assistant test harness, web deps, git hooks
just check     # everything CI runs: lint, firmware tests + build, HA tests, site build
```

You need Python 3.14, Node 20+ and [`just`](https://github.com/casey/just). `just` with no arguments
lists every recipe.

## Where things live

| Path | What | Test it with |
|---|---|---|
| `hardware/cad/` | FreeCAD build script: every part, plus a validation report (collisions, access paths) | run it in FreeCAD; `REPORT` must be clean |
| `hardware/production/` | generated STL/STEP + BOM: never edit by hand | `export_production.py` |
| `hardware/electronics/` | circuit, carrier board, harness | |
| `firmware/lynx/` | the device firmware | `just fw-test`, `just fw-build`, then on a board |
| `custom_components/lynx/` | Home Assistant integration | `just hass-test` |
| `web/` | landing page, build guide, web flasher | `just web-dev` |
| `docs/` | the documentation hub | |

## The gates

Each one runs locally (pre-commit / `just check`) and in CI:

- **ruff** (lint + format) on the integration and its tests
- **firmware native tests**: motion, MCP, auth, network plan (`lib/` is pure C++, no hardware)
- **firmware build** for the XIAO, including the merged `lynx.bin` the web flasher serves
- **Home Assistant**: pytest against a pinned HA release, **hassfest**, the **HACS** validator
- `strings.json` and `translations/en.json` stay identical
- the site builds

## Conventions

- **Commits** follow [Conventional Commits](https://www.conventionalcommits.org): `feat(firmware): …`,
  `fix(hass): …`, `docs: …`. Scopes: `firmware`, `hass`, `web`, `hardware`, `docs`, `ci`.
- **Every source file** starts with an `SPDX-License-Identifier` line: `MIT` for code,
  `CERN-OHL-S-2.0` for hardware, `CC-BY-SA-4.0` for docs.
- **Logic goes in `firmware/lynx/lib/`, with a native test.** `src/` is only the glue to hardware:
  if it can be tested without a board, it belongs in `lib/`.
- **Hardware changes start in the CAD script.** Change it, run the validation report, re-export.
  The STLs, the BOM and the website's model all come from it.
- **Anything you couldn't test on hardware, say so** in the PR. The firmware README keeps a bring-up
  checklist of what has been verified on a real board.

## Bring-up on a real board

```sh
just fw-flash                 # LYNX_PORT=/dev/cu.usbmodem… to choose
just fw-monitor               # `info`, `status`, `token`, `help`
export LYNX_HOST=lynx-42e0.local LYNX_TOKEN=…
just info && just status && just look 30 45 && just snap
```

Then work through the [bring-up checklist](firmware/lynx/README.md#bring-up-checklist) and record what you saw.

## Releasing

1. Bump the version in `firmware/lynx/src/app.h`, `custom_components/lynx/manifest.json` and
   `web/public/flash/manifest.json`, and add a section to `CHANGELOG.md`.
2. `just release 0.4.0`: the tag triggers CI, which builds the firmware and attaches
   `lynx-v0.4.0.bin` to the GitHub release. The site's flasher redeploys from `main`.

## Reporting

Use the issue templates: **Bug** for software, **Build report** for how your build went (fits, servo
clones, printers). Build reports are how the test coupons and tolerances get better.
