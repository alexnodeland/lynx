# SPDX-License-Identifier: MIT
"""The lynx camera: snapshots, live MJPEG, and the lynx.look action."""

from __future__ import annotations

from aiohttp import web
from homeassistant.components.camera import Camera
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import entity_platform, service
from homeassistant.helpers.aiohttp_client import async_aiohttp_proxy_web, async_get_clientsession
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import LynxError
from .coordinator import LynxConfigEntry, LynxCoordinator
from .entity import LynxEntity

PARALLEL_UPDATES = 0


async def async_setup_entry(
    hass: HomeAssistant, entry: LynxConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([LynxCamera(entry.runtime_data)])
    if not hasattr(service, "async_register_platform_entity_service"):  # pre-2026 Home Assistant
        from . import CAMERA_ACTIONS

        platform = entity_platform.async_get_current_platform()
        for name, schema, func in CAMERA_ACTIONS:
            platform.async_register_entity_service(name, schema, func)


class LynxCamera(LynxEntity, Camera):
    """Stills from /snapshot.jpg; live view proxies the device's MJPEG stream.

    No stream_source(): the device serves one stream viewer at a time, and HA's stream
    component would hold that slot permanently through ffmpeg.
    """

    _attr_name = None  # the device's main feature: the entity takes the device name
    _attr_brand = "lynx"
    _attr_model = "lynx pan/tilt camera"

    def __init__(self, coordinator: LynxCoordinator) -> None:
        LynxEntity.__init__(self, coordinator, "camera")
        Camera.__init__(self)

    @property
    def is_on(self) -> bool:
        return bool(self.status.get("camera", {}).get("ready", True))

    async def async_camera_image(self, width: int | None = None, height: int | None = None) -> bytes | None:
        try:
            return await self.coordinator.client.snapshot()
        except LynxError:
            return None

    async def handle_async_mjpeg_stream(self, request: web.Request) -> web.StreamResponse | None:
        client = self.coordinator.client
        session = async_get_clientsession(self.hass)
        return await async_aiohttp_proxy_web(
            self.hass, request, session.get(client.stream_url, headers=client.auth_headers)
        )

    async def async_save_preset(self, name: str, pan: float | None = None, tilt: float | None = None) -> None:
        """lynx.save_preset: name the current position (or the one given)."""
        extra = {k: v for k, v in (("pan", pan), ("tilt", tilt)) if v is not None}
        try:
            await self.coordinator.client.preset("save", name, **extra)
        except LynxError as err:
            raise HomeAssistantError(f"lynx did not save the preset: {err}") from err
        await self.coordinator.async_request_refresh()

    async def async_delete_preset(self, name: str) -> None:
        try:
            await self.coordinator.client.preset("delete", name)
        except LynxError as err:
            raise HomeAssistantError(f"lynx did not delete the preset: {err}") from err
        await self.coordinator.async_request_refresh()

    async def async_look(
        self, pan: float | None = None, tilt: float | None = None, duration: int | None = None
    ) -> None:
        """lynx.look: move to an absolute position. Omitted axes stay where they are."""
        if pan is None and tilt is None:
            raise HomeAssistantError("lynx.look needs pan and/or tilt")
        try:
            self.coordinator.apply(await self.coordinator.client.position(pan, tilt, duration))
        except LynxError as err:
            raise HomeAssistantError(f"lynx did not move: {err}") from err
