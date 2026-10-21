# SPDX-License-Identifier: MIT
"""Fixtures: a fake lynx answering at 192.168.1.40, mocked at the aiohttp layer."""

from __future__ import annotations

import copy
from typing import Any

import pytest
from homeassistant.config_entries import ConfigEntryState
from homeassistant.const import CONF_HOST
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.lynx.const import CONF_TOKEN, DOMAIN

HOST = "192.168.1.40"
TOKEN = "9f3c1e0a7b2d4c6e8f0a1b2c3d4e5f60"
DEV_ID = "44b176af42e0"
BASE = f"http://{HOST}"

INFO: dict[str, Any] = {
    "name": "lynx-42e0",
    "id": DEV_ID,
    "product": "lynx",
    "version": "0.3.0",
    "mdns": "lynx-42e0.local",
    "ip": HOST,
}

STATUS: dict[str, Any] = {
    "pan": {"pos": 0, "target": 0, "moving": False, "attached": False, "min": -88, "max": 88},
    "tilt": {"pos": 45, "target": 45, "moving": False, "attached": False, "min": -58, "max": 58},
    "sweeping": False,
    "restTilt": 45,
    "camera": {"ready": True, "framesize": "VGA", "quality": 12, "vflip": False, "hmirror": False, "brightness": 0},
    "network": {
        "name": "lynx-42e0",
        "id": DEV_ID,
        "mdns": "lynx-42e0.local",
        "ip": HOST,
        "wifi": "connected",
        "rssi": -48,
        "setupHotspot": False,
    },
    "detect": {
        "enabled": True,
        "model": "face",
        "range": "far",
        "count": 1,
        "present": True,
        "fps": 3.6,
        "follow": False,
        "tracking": False,
    },
    "presets": [{"name": "door", "pan": 60, "tilt": 50}, {"name": "desk", "pan": -30, "tilt": 40}],
    "patrol": {"enabled": True, "dwellS": 10, "presets": ["door", "desk"], "at": "desk"},
    "version": "0.3.0",
    "uptimeMs": 600000,
    "freeHeap": 224000,
    "freePsram": 7600000,
}


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    yield


@pytest.fixture(autouse=True)
async def unload_entries_after(hass):
    """Unload every lynx entry when a test ends, which cancels its events listener (as HA does on shutdown)."""
    yield
    await hass.async_block_till_done()
    for e in hass.config_entries.async_entries(DOMAIN):
        if e.state.recoverable or e.state is ConfigEntryState.LOADED:
            await hass.config_entries.async_unload(e.entry_id)
    await hass.async_block_till_done()


@pytest.fixture(autouse=True)
def no_pushed_events(aioclient_mock):
    """By default the device pushes nothing on :81/events, so tests run on polling alone."""
    aioclient_mock.get(f"{BASE}:81/events", exc=TimeoutError)


@pytest.fixture
def status() -> dict[str, Any]:
    return copy.deepcopy(STATUS)


@pytest.fixture
def lynx(aioclient_mock, status):
    """Register the device's endpoints; returns the mocker so tests can inspect calls."""
    aioclient_mock.get(f"{BASE}/api/info", json=INFO)
    aioclient_mock.get(f"{BASE}/api/status", json=status)
    aioclient_mock.post(f"{BASE}/api/position", json=status)
    aioclient_mock.post(f"{BASE}/api/center", json=status)
    aioclient_mock.post(f"{BASE}/api/sweep", json=status)
    aioclient_mock.post(f"{BASE}/api/camera", json=status["camera"])
    aioclient_mock.post(f"{BASE}/api/detect", json={"enabled": True})
    aioclient_mock.post(f"{BASE}/api/follow", json={"enabled": True})
    aioclient_mock.post(f"{BASE}/api/presets", json={"presets": status["presets"], "patrol": status["patrol"]})
    aioclient_mock.post(f"{BASE}/api/patrol", json={"presets": status["presets"], "patrol": status["patrol"]})
    aioclient_mock.get(f"{BASE}/snapshot.jpg", content=b"\xff\xd8\xff\xd9")
    return aioclient_mock


@pytest.fixture
def entry(hass) -> MockConfigEntry:
    e = MockConfigEntry(domain=DOMAIN, unique_id=DEV_ID, title="lynx-42e0", data={CONF_HOST: HOST, CONF_TOKEN: TOKEN})
    e.add_to_hass(hass)
    return e


async def setup(hass, entry) -> None:
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
