# SPDX-License-Identifier: MIT
"""Camera resolution."""

from __future__ import annotations

from homeassistant.components.select import SelectEntity
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import LynxError
from .const import FRAMESIZES
from .coordinator import LynxConfigEntry, LynxCoordinator
from .entity import LynxEntity

PARALLEL_UPDATES = 1


async def async_setup_entry(
    hass: HomeAssistant, entry: LynxConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([LynxResolution(entry.runtime_data)])


class LynxResolution(LynxEntity, SelectEntity):
    _attr_translation_key = "resolution"
    _attr_entity_category = EntityCategory.CONFIG
    _attr_options = FRAMESIZES

    def __init__(self, coordinator: LynxCoordinator) -> None:
        super().__init__(coordinator, "resolution")

    @property
    def current_option(self) -> str | None:
        fs = self.status.get("camera", {}).get("framesize")
        return fs if fs in FRAMESIZES else None

    async def async_select_option(self, option: str) -> None:
        try:
            await self.coordinator.client.set_camera(framesize=option)
        except LynxError as err:
            raise HomeAssistantError(f"lynx rejected the resolution: {err}") from err
        await self.coordinator.async_request_refresh()
