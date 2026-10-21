# SPDX-License-Identifier: MIT
"""Setup, entities, actions, the camera, auth failure and diagnostics."""

from __future__ import annotations

import json

import pytest
from homeassistant.components.camera import async_get_image
from homeassistant.config_entries import ConfigEntryState
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from pytest_homeassistant_custom_component.components.diagnostics import get_diagnostics_for_config_entry

from custom_components.lynx.const import DOMAIN

from .conftest import BASE, DEV_ID, INFO, TOKEN, setup


def posts(mock, path: str) -> list:
    return [c for c in mock.mock_calls if c[0] == "POST" and str(c[1]).endswith(path)]


async def test_setup_creates_device_and_entities(hass, entry, lynx):
    await setup(hass, entry)
    assert entry.state is ConfigEntryState.LOADED

    (device,) = dr.async_entries_for_config_entry(dr.async_get(hass), entry.entry_id)
    assert (DOMAIN, DEV_ID) in device.identifiers
    assert device.name == "lynx-42e0"
    assert device.sw_version == "0.3.0"
    assert (dr.CONNECTION_NETWORK_MAC, "44:b1:76:af:42:e0") in device.connections

    assert hass.states.get("camera.lynx_42e0") is not None
    assert hass.states.get("number.lynx_42e0_pan").state == "0.0"
    tilt = hass.states.get("number.lynx_42e0_tilt")
    assert tilt.state == "45.0"
    assert tilt.attributes["min"] == -58 and tilt.attributes["max"] == 58
    assert hass.states.get("select.lynx_42e0_resolution").state == "VGA"
    assert hass.states.get("switch.lynx_42e0_flip_image").state == "off"
    assert hass.states.get("binary_sensor.lynx_42e0_moving").state == "off"
    assert hass.states.get("sensor.lynx_42e0_wi_fi_signal").state == "-48"
    assert hass.states.get("button.lynx_42e0_rest_pose") is not None
    # off by default: noisy diagnostics
    assert er.async_get(hass).async_get("number.lynx_42e0_jpeg_quality").disabled_by is not None


async def test_set_pan_posts_only_pan(hass, entry, lynx):
    await setup(hass, entry)
    await hass.services.async_call(
        "number", "set_value", {"entity_id": "number.lynx_42e0_pan", "value": 30}, blocking=True
    )
    call = posts(lynx, "/api/position")[-1]
    assert call[2] == {"pan": 30.0}
    assert call[3]["Authorization"] == f"Bearer {TOKEN}"


async def test_buttons(hass, entry, lynx):
    await setup(hass, entry)
    await hass.services.async_call("button", "press", {"entity_id": "button.lynx_42e0_rest_pose"}, blocking=True)
    await hass.services.async_call("button", "press", {"entity_id": "button.lynx_42e0_sweep"}, blocking=True)
    assert posts(lynx, "/api/center") and posts(lynx, "/api/sweep")


async def test_rotate_token_updates_the_entry(hass, entry, lynx):
    await setup(hass, entry)
    await hass.services.async_call("button", "press", {"entity_id": "button.lynx_42e0_rotate_token"}, blocking=True)
    assert posts(lynx, "/api/token/rotate")[-1][3]["Authorization"] == f"Bearer {TOKEN}"
    assert entry.data["token"] == "b" * 32
    await hass.services.async_call("button", "press", {"entity_id": "button.lynx_42e0_rest_pose"}, blocking=True)
    assert posts(lynx, "/api/center")[-1][3]["Authorization"] == f"Bearer {'b' * 32}"


async def test_look_action(hass, entry, lynx):
    await setup(hass, entry)
    await hass.services.async_call(
        DOMAIN, "look", {"entity_id": "camera.lynx_42e0", "pan": -40, "tilt": 30, "duration": 900}, blocking=True
    )
    assert posts(lynx, "/api/position")[-1][2] == {"pan": -40.0, "tilt": 30.0, "ms": 900}
    with pytest.raises(HomeAssistantError):
        await hass.services.async_call(DOMAIN, "look", {"entity_id": "camera.lynx_42e0"}, blocking=True)


async def test_camera_settings(hass, entry, lynx):
    await setup(hass, entry)
    await hass.services.async_call(
        "select", "select_option", {"entity_id": "select.lynx_42e0_resolution", "option": "SVGA"}, blocking=True
    )
    await hass.services.async_call("switch", "turn_on", {"entity_id": "switch.lynx_42e0_flip_image"}, blocking=True)
    bodies = [c[2] for c in posts(lynx, "/api/camera")]
    assert {"framesize": "SVGA"} in bodies and {"vflip": True} in bodies


async def test_detection_entities(hass, entry, lynx):
    await setup(hass, entry)
    det = hass.states.get("binary_sensor.lynx_42e0_detected")
    assert det.state == "on"
    assert det.attributes["model"] == "face"
    assert hass.states.get("sensor.lynx_42e0_detected_count").state == "1"
    assert hass.states.get("switch.lynx_42e0_detection").state == "on"
    assert hass.states.get("switch.lynx_42e0_follow").state == "off"
    assert hass.states.get("select.lynx_42e0_detect").state == "face"
    assert hass.states.get("select.lynx_42e0_detection_range").state == "far"
    await hass.services.async_call("switch", "turn_on", {"entity_id": "switch.lynx_42e0_follow"}, blocking=True)
    await hass.services.async_call(
        "select", "select_option", {"entity_id": "select.lynx_42e0_detect", "option": "cat"}, blocking=True
    )
    await hass.services.async_call("switch", "turn_off", {"entity_id": "switch.lynx_42e0_detection"}, blocking=True)
    assert posts(lynx, "/api/follow")[-1][2] == {"enabled": True}
    bodies = [c[2] for c in posts(lynx, "/api/detect")]
    assert {"model": "cat"} in bodies and {"enabled": False} in bodies


async def test_custom_model_option_only_when_installed(hass, entry, aioclient_mock, status):
    aioclient_mock.get(f"{BASE}/api/info", json=INFO)
    status["detect"]["customModel"] = "lynx-package"
    aioclient_mock.get(f"{BASE}/api/status", json=status)
    await setup(hass, entry)
    assert "custom" in hass.states.get("select.lynx_42e0_detect").attributes["options"]


async def test_no_custom_option_without_a_model(hass, entry, lynx):
    await setup(hass, entry)
    assert hass.states.get("select.lynx_42e0_detect").attributes["options"] == ["face", "cat"]


async def test_detected_unavailable_when_detection_off(hass, entry, aioclient_mock, status):
    status["detect"]["enabled"] = False
    aioclient_mock.get(f"{BASE}/api/info", json=INFO)
    aioclient_mock.get(f"{BASE}/api/status", json=status)
    await setup(hass, entry)
    assert hass.states.get("binary_sensor.lynx_42e0_detected").state == "unavailable"
    assert hass.states.get("sensor.lynx_42e0_detected_count").state == "unknown"


async def test_presets_and_patrol(hass, entry, lynx):
    await setup(hass, entry)
    sel = hass.states.get("select.lynx_42e0_preset")
    assert sel.attributes["options"] == ["door", "desk"]
    assert sel.state == "desk"  # the patrol's current stop
    assert hass.states.get("switch.lynx_42e0_patrol").state == "on"
    await hass.services.async_call(
        "select", "select_option", {"entity_id": "select.lynx_42e0_preset", "option": "door"}, blocking=True
    )
    await hass.services.async_call(
        DOMAIN, "save_preset", {"entity_id": "camera.lynx_42e0", "name": "window", "pan": 10}, blocking=True
    )
    await hass.services.async_call(
        DOMAIN, "delete_preset", {"entity_id": "camera.lynx_42e0", "name": "desk"}, blocking=True
    )
    await hass.services.async_call("switch", "turn_off", {"entity_id": "switch.lynx_42e0_patrol"}, blocking=True)
    bodies = [c[2] for c in posts(lynx, "/api/presets")]
    assert {"action": "go", "name": "door"} in bodies
    assert {"action": "save", "name": "window", "pan": 10.0} in bodies
    assert {"action": "delete", "name": "desk"} in bodies
    assert posts(lynx, "/api/patrol")[-1][2] == {"enabled": False}


async def test_pushed_events_update_entities(hass, entry, aioclient_mock, status):
    import json as _json

    aioclient_mock.clear_requests()  # replace the default "no events" mock
    aioclient_mock.get(f"{BASE}/api/info", json=INFO)
    aioclient_mock.get(f"{BASE}/api/status", json=status)
    pushed = {**status, "pan": {**status["pan"], "target": 33}}
    body = f"retry: 3000\n\nevent: status\ndata: {_json.dumps(pushed)}\n\n: ping\n\n"
    aioclient_mock.get(f"{BASE}:81/events", text=body)
    await setup(hass, entry)
    await hass.async_block_till_done()
    assert hass.states.get("number.lynx_42e0_pan").state == "33.0"
    events_calls = [c for c in aioclient_mock.mock_calls if str(c[1]).endswith(":81/events")]
    assert events_calls and events_calls[0][3]["Authorization"] == f"Bearer {TOKEN}"


async def test_recording_entities(hass, entry, lynx):
    await setup(hass, entry)
    assert hass.states.get("sensor.lynx_42e0_sd_card_used").state == "25.0"
    assert hass.states.get("switch.lynx_42e0_record_on_detection").state == "off"
    await hass.services.async_call(
        "switch", "turn_on", {"entity_id": "switch.lynx_42e0_record_on_detection"}, blocking=True
    )
    await hass.services.async_call(
        "number", "set_value", {"entity_id": "number.lynx_42e0_timelapse_interval", "value": 15}, blocking=True
    )
    bodies = [c[2] for c in posts(lynx, "/api/sd")]
    assert {"onDetect": True} in bodies and {"timelapseMin": 15} in bodies


async def test_recording_unavailable_without_a_card(hass, entry, aioclient_mock, status):
    status["sd"] = {"mounted": False, "onDetect": False, "timelapseMin": 0}
    aioclient_mock.get(f"{BASE}/api/info", json=INFO)
    aioclient_mock.get(f"{BASE}/api/status", json=status)
    await setup(hass, entry)
    assert hass.states.get("switch.lynx_42e0_record_on_detection").state == "unavailable"
    assert hass.states.get("number.lynx_42e0_timelapse_interval").state == "unavailable"
    assert hass.states.get("sensor.lynx_42e0_sd_card_used").state == "unknown"


async def test_camera_image(hass, entry, lynx):
    await setup(hass, entry)
    image = await async_get_image(hass, "camera.lynx_42e0")
    assert image.content == b"\xff\xd8\xff\xd9"


async def test_rejected_token_starts_reauth(hass, entry, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/info", json={"id": DEV_ID, "product": "lynx", "name": "lynx-42e0"})
    aioclient_mock.get(f"{BASE}/api/status", status=401)
    assert not await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.SETUP_ERROR
    flows = hass.config_entries.flow.async_progress()
    assert any(f["context"]["source"] == "reauth" for f in flows)


async def test_unreachable_retries(hass, entry, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/info", exc=TimeoutError)
    await hass.config_entries.async_setup(entry.entry_id)
    assert entry.state is ConfigEntryState.SETUP_RETRY


async def test_unload(hass, entry, lynx):
    await setup(hass, entry)
    assert await hass.config_entries.async_unload(entry.entry_id)
    assert entry.state is ConfigEntryState.NOT_LOADED


async def test_diagnostics_redact_token(hass, hass_client, entry, lynx):
    await setup(hass, entry)
    diag = await get_diagnostics_for_config_entry(hass, hass_client, entry)
    assert TOKEN not in json.dumps(diag)
    assert diag["status"]["tilt"]["target"] == 45
