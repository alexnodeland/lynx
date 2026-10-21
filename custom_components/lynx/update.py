# SPDX-License-Identifier: MIT
"""Firmware updates from GitHub releases, installed over Wi-Fi (the device rolls back by itself if one fails)."""

from __future__ import annotations

import asyncio
import hashlib
import logging
from datetime import timedelta
from typing import Any

import aiohttp
from homeassistant.components.update import UpdateDeviceClass, UpdateEntity, UpdateEntityFeature
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import LynxError
from .coordinator import LynxConfigEntry, LynxCoordinator
from .entity import LynxEntity

_LOGGER = logging.getLogger(__name__)
PARALLEL_UPDATES = 1
RELEASES_URL = "https://api.github.com/repos/alexnodeland/lynx/releases/latest"
CHECK_EVERY = timedelta(hours=6)
_sleep = asyncio.sleep  # its own name, so tests can speed up the wait for the reboot alone


def _ver(v: str | None) -> tuple:
    try:
        return tuple(int(p) for p in (v or "").lstrip("v").split("."))
    except ValueError:
        return ()


class LynxReleases(DataUpdateCoordinator[dict[str, Any]]):
    """The latest lynx release on GitHub: one check per entry every 6 hours."""

    def __init__(self, hass: HomeAssistant, entry: LynxConfigEntry) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name="lynx releases", update_interval=CHECK_EVERY)

    async def _async_update_data(self) -> dict[str, Any]:
        session = async_get_clientsession(self.hass)
        try:
            async with asyncio.timeout(20):
                resp = await session.get(RELEASES_URL, headers={"Accept": "application/vnd.github+json"})
                if resp.status == 404:
                    return {}  # no release yet
                resp.raise_for_status()
                return await resp.json()
        except (TimeoutError, aiohttp.ClientError) as err:
            raise UpdateFailed(f"GitHub releases: {err}") from err


async def async_setup_entry(
    hass: HomeAssistant, entry: LynxConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    releases = LynxReleases(hass, entry)
    await releases.async_refresh()  # a failed check isn't fatal: the entity just shows no update
    async_add_entities([LynxFirmwareUpdate(entry.runtime_data, releases)])


class LynxFirmwareUpdate(LynxEntity, UpdateEntity):
    _attr_translation_key = "firmware"
    _attr_device_class = UpdateDeviceClass.FIRMWARE
    _attr_supported_features = (
        UpdateEntityFeature.INSTALL | UpdateEntityFeature.PROGRESS | UpdateEntityFeature.RELEASE_NOTES
    )

    def __init__(self, coordinator: LynxCoordinator, releases: LynxReleases) -> None:
        super().__init__(coordinator, "firmware")
        self._releases = releases

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(self._releases.async_add_listener(self.async_write_ha_state))

    @property
    def _release(self) -> dict[str, Any]:
        return self._releases.data or {}

    @property
    def installed_version(self) -> str | None:
        return self.status.get("version")

    @property
    def latest_version(self) -> str | None:
        tag = self._release.get("tag_name")
        if not tag:
            return self.installed_version  # nothing published (or GitHub unreachable): no update to offer
        latest = tag.lstrip("v")
        # never offer a downgrade (a local build newer than the last release)
        return latest if _ver(latest) >= _ver(self.installed_version) else self.installed_version

    @property
    def release_url(self) -> str | None:
        return self._release.get("html_url")

    @property
    def release_summary(self) -> str | None:
        body = self._release.get("body") or ""
        return body[:250] or None

    async def async_release_notes(self) -> str | None:
        return self._release.get("body")

    def _asset(self, prefix: str, suffix: str) -> dict[str, Any] | None:
        for a in self._release.get("assets", []):
            if a.get("name", "").startswith(prefix) and a.get("name", "").endswith(suffix):
                return a
        return None

    async def async_install(self, version: str | None, backup: bool, **kwargs: Any) -> None:
        app, manifest = self._asset("lynx-app-", ".bin"), self._asset("lynx-v", ".json")
        if not app:
            raise HomeAssistantError("this release has no lynx-app-*.bin to install over the air")
        session = async_get_clientsession(self.hass)
        self._attr_in_progress = True
        self._attr_update_percentage = 0
        self.async_write_ha_state()
        try:
            want = None
            if manifest:
                async with asyncio.timeout(30):
                    m = await (await session.get(manifest["browser_download_url"])).json(content_type=None)
                want = (m.get("app") or {}).get("sha256")
            async with asyncio.timeout(120):
                resp = await session.get(app["browser_download_url"])
                resp.raise_for_status()
                image = await resp.read()
            if want and hashlib.sha256(image).hexdigest() != want:
                raise HomeAssistantError("the downloaded image doesn't match the release's SHA-256")
            self._attr_update_percentage = 40
            self.async_write_ha_state()
            await self.coordinator.client.update_firmware(image, want)  # device checks the hash again, then reboots
            self._attr_update_percentage = 80
            self.async_write_ha_state()
            target = (self._release.get("tag_name") or "").lstrip("v")
            for _ in range(45):  # back within ~90 s on the new version (or rolled back to the old one)
                await _sleep(2)
                try:
                    info = await self.coordinator.client.info()
                except LynxError:
                    continue
                if info.get("version") == target:
                    await self.coordinator.async_request_refresh()
                    return
            raise HomeAssistantError(
                f"lynx didn't come back on {target}; it keeps (or rolled back to) the old firmware"
            )
        except (TimeoutError, aiohttp.ClientError, LynxError) as err:
            raise HomeAssistantError(f"update failed: {err}") from err
        finally:
            self._attr_in_progress = False
            self._attr_update_percentage = None
            self.async_write_ha_state()
