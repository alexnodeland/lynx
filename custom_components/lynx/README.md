# lynx for Home Assistant

[![Home Assistant](https://github.com/alexnodeland/lynx/actions/workflows/hass.yml/badge.svg)](https://github.com/alexnodeland/lynx/actions/workflows/hass.yml)
[![HACS](https://img.shields.io/badge/HACS-custom-41bdf5)](https://hacs.xyz)

A local integration for the lynx pan/tilt camera. Home Assistant finds lynx on your
network by itself. You paste its token once, and it becomes a device with a camera,
pan/tilt controls, camera settings and diagnostics, ready for dashboards, automations and Assist.
It runs entirely on your LAN: no cloud, no MQTT broker.

## Install

1. In HACS: **⋮ › Custom repositories**, add `https://github.com/alexnodeland/lynx` as an **Integration**,
   then install **lynx pan/tilt camera** and restart Home Assistant.
   (By hand instead: copy `custom_components/lynx` into your config's `custom_components/`.)
2. **Settings › Devices & services**. A discovered **lynx-xxxx** appears; click **Add**.
   If it doesn't, use **Add integration › lynx** and enter its address.
3. Paste the device token, the one the setup page showed. The serial command `token` prints it too.

## What you get

| Entity | What it does |
|--------|--------------|
| `camera.lynx_xxxx` | stills from `/snapshot.jpg`, live view proxied from the MJPEG stream |
| `number.…_pan`, `number.…_tilt` | sliders; ranges follow the device's soft limits |
| `button.…_rest_pose`, `button.…_sweep` | rest pose (pan 0, tilt 45) and the demo sweep |
| `select.…_resolution` | QVGA … UXGA |
| `switch.…_flip_image`, `switch.…_mirror_image` | for however the board sits in the pod |
| `number.…_brightness`, `number.…_jpeg_quality` | camera tuning (quality is off by default) |
| `binary_sensor.…_moving` | the head is moving or sweeping |
| `binary_sensor.…_detected` | occupancy: a face (or cat face) seen by the on-device detector in the last 10 s |
| `sensor.…_detected_count` | how many are in view |
| `switch.…_detection`, `switch.…_follow` | on-device detection, and follow mode (keeps the nearest face in frame) |
| `select.…_detect`, `select.…_detection_range` | faces or cat faces; near or far |
| `select.…_preset`, `switch.…_patrol` | go to a named position; tour them on a loop |
| `sensor.…_wi_fi_signal`, `sensor.…_last_boot`, `sensor.…_free_memory` | diagnostics |

Plus the actions **`lynx.look`**, **`lynx.save_preset`** and **`lynx.delete_preset`**, which target the camera:

```yaml
action: lynx.look
target:
  entity_id: camera.lynx_42e0
data:
  pan: -40       # degrees, 0 = straight ahead
  tilt: 45       # 0 = lens straight up, 45 = rest
  duration: 900  # ms, optional
```

Example: look at the front door when it opens, then take a snapshot.

```yaml
triggers:
  - trigger: state
    entity_id: binary_sensor.front_door
    to: "on"
actions:
  - action: lynx.look
    target: { entity_id: camera.lynx_42e0 }
    data: { pan: 60, tilt: 50 }
  - delay: 1
  - action: camera.snapshot
    target: { entity_id: camera.lynx_42e0 }
    data: { filename: /media/lynx/door_{{ now().strftime('%Y%m%d_%H%M%S') }}.jpg }
```

Or: turn the lights on when lynx sees someone, and follow them while they're there.

```yaml
triggers:
  - trigger: state
    entity_id: binary_sensor.lynx_42e0_detected
    to: "on"
actions:
  - action: switch.turn_on
    target: { entity_id: switch.lynx_42e0_follow }
  - action: light.turn_on
    target: { entity_id: light.study }
```

## Good to know

- **Instant updates.** The integration listens to the device's `/events` stream, so entities change the moment
  the head moves or a face appears. Polling continues as a fallback (every 60 s while events flow, 10 s if
  they stop). That listener uses one of the device's 4 connections on port 81.
- **Live view.** Up to 3 more viewers (dashboards, VLC) can watch at once. The integration doesn't hand the
  stream to HA's `stream`/ffmpeg pipeline, which would hold a connection permanently.
- **Token rotated?** Run `token new` on the serial console and HA asks you for the new token.
- **New IP address?** Discovery updates it on its own; **Reconfigure** sets it by hand.
- Polling every 10 s; commands update the state right away from the device's reply.

## Developing

```sh
python3.14 -m venv .venv && . .venv/bin/activate
pip install pytest-homeassistant-custom-component PyTurboJPEG zeroconf ruff
pytest              # tests/hass: config flow, entities, actions, camera, reauth, diagnostics
ruff check . && ruff format --check .
```

CI also runs **hassfest** (Home Assistant's own manifest/translation checks) and the HACS validator.
