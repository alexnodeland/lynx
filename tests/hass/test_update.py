# SPDX-License-Identifier: MIT
"""Firmware updates from GitHub releases."""

from __future__ import annotations

import hashlib
from unittest.mock import patch

import pytest
from homeassistant.exceptions import HomeAssistantError

from custom_components.lynx.update import RELEASES_URL

from .conftest import BASE, INFO, setup

IMAGE = b"\xe9" + b"\x00" * 2047
GH = "https://github.com/alexnodeland/lynx/releases/download/v0.4.0"


def release(sha: str) -> tuple[dict, dict]:
    rel = {
        "tag_name": "v0.4.0",
        "html_url": "https://github.com/alexnodeland/lynx/releases/tag/v0.4.0",
        "body": "## 0.4.0\n- OTA updates",
        "assets": [
            {"name": "lynx-v0.4.0.bin", "browser_download_url": f"{GH}/lynx-v0.4.0.bin"},
            {"name": "lynx-app-v0.4.0.bin", "browser_download_url": f"{GH}/lynx-app-v0.4.0.bin"},
            {"name": "lynx-v0.4.0.json", "browser_download_url": f"{GH}/lynx-v0.4.0.json"},
        ],
    }
    return rel, {"version": "0.4.0", "app": {"file": "lynx-app-v0.4.0.bin", "sha256": sha}}


def mock_release(aioclient_mock, status, sha: str, back_version: str = "0.4.0") -> None:
    aioclient_mock.clear_requests()
    rel, manifest = release(sha)
    aioclient_mock.get(RELEASES_URL, json=rel)
    aioclient_mock.get(f"{GH}/lynx-v0.4.0.json", json=manifest)
    aioclient_mock.get(f"{GH}/lynx-app-v0.4.0.bin", content=IMAGE)
    aioclient_mock.get(f"{BASE}:81/events", exc=TimeoutError)
    aioclient_mock.get(f"{BASE}/api/status", json=status)
    aioclient_mock.post(f"{BASE}/api/update", json={"ok": True, "slot": "app1"})
    # first /api/info at setup (old version), later ones after the reboot
    aioclient_mock.get(f"{BASE}/api/info", json={**INFO, "version": back_version})


@pytest.fixture
def fast_sleep():
    async def no_wait(*_):
        return None

    with patch("custom_components.lynx.update._sleep", no_wait):
        yield


async def test_no_release_means_no_update(hass, entry, lynx):
    await setup(hass, entry)
    st = hass.states.get("update.lynx_42e0_firmware")
    assert st.state == "off"
    assert st.attributes["installed_version"] == "0.3.0"


async def test_offers_newer_release(hass, entry, aioclient_mock, status):
    mock_release(aioclient_mock, status, hashlib.sha256(IMAGE).hexdigest())
    await setup(hass, entry)
    st = hass.states.get("update.lynx_42e0_firmware")
    assert st.state == "on"
    assert st.attributes["latest_version"] == "0.4.0"
    assert st.attributes["release_url"].endswith("/v0.4.0")


async def test_never_offers_a_downgrade(hass, entry, aioclient_mock, status):
    mock_release(aioclient_mock, {**status, "version": "0.5.0"}, "x" * 64)
    await setup(hass, entry)
    assert hass.states.get("update.lynx_42e0_firmware").state == "off"


async def test_install_checks_hash_uploads_and_waits(hass, entry, aioclient_mock, status, fast_sleep):
    sha = hashlib.sha256(IMAGE).hexdigest()
    mock_release(aioclient_mock, status, sha)
    await setup(hass, entry)
    await hass.services.async_call("update", "install", {"entity_id": "update.lynx_42e0_firmware"}, blocking=True)
    up = [c for c in aioclient_mock.mock_calls if c[0] == "POST" and str(c[1]).endswith("/api/update")]
    assert len(up) == 1
    assert up[0][2] == IMAGE
    assert up[0][3]["X-Lynx-SHA256"] == sha


async def test_install_refuses_a_bad_download(hass, entry, aioclient_mock, status, fast_sleep):
    mock_release(aioclient_mock, status, "0" * 64)
    await setup(hass, entry)
    with pytest.raises(HomeAssistantError, match="SHA-256"):
        await hass.services.async_call("update", "install", {"entity_id": "update.lynx_42e0_firmware"}, blocking=True)
    assert not [c for c in aioclient_mock.mock_calls if c[0] == "POST" and str(c[1]).endswith("/api/update")]


async def test_install_reports_a_rollback(hass, entry, aioclient_mock, status, fast_sleep):
    mock_release(aioclient_mock, status, hashlib.sha256(IMAGE).hexdigest(), back_version="0.3.0")
    await setup(hass, entry)
    with pytest.raises(HomeAssistantError, match="didn't come back"):
        await hass.services.async_call("update", "install", {"entity_id": "update.lynx_42e0_firmware"}, blocking=True)
