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
    c = entry.runtime_data
    async_add_entities(
        [
            LynxResolution(c),
            LynxPresetSelect(c),
            LynxDetectSelect(c, "detect_model", "model", ["face", "cat", "custom"]),
            LynxDetectSelect(c, "detect_range", "range", ["near", "far"]),
        ]
    )


class LynxPresetSelect(LynxEntity, SelectEntity):
    """Go to a named position. Shows the patrol's current stop while it tours."""

    _attr_translation_key = "preset"

    def __init__(self, coordinator: LynxCoordinator) -> None:
        super().__init__(coordinator, "preset")

    @property
    def options(self) -> list[str]:
        return [p["name"] for p in self.status.get("presets", [])]

    @property
    def available(self) -> bool:
        return super().available and bool(self.options)

    @property
    def current_option(self) -> str | None:
        at = self.status.get("patrol", {}).get("at")
        return at if at in self.options else None

    async def async_select_option(self, option: str) -> None:
        try:
            self.coordinator.apply(await self.coordinator.client.preset("go", option))
        except LynxError as err:
            raise HomeAssistantError(f"lynx did not go to {option}: {err}") from err
        await self.coordinator.async_request_refresh()


class LynxDetectSelect(LynxEntity, SelectEntity):
    """What the on-device detector looks for (face, cat), and how far away (near, far)."""

    _attr_entity_category = EntityCategory.CONFIG

    def __init__(self, coordinator: LynxCoordinator, key: str, field: str, options: list[str]) -> None:
        super().__init__(coordinator, key)
        self._attr_translation_key = key
        self._attr_options = options
        self._field = field

    @property
    def options(self) -> list[str]:
        # `custom` only when the device has an Edge Impulse model installed
        if self._field == "model" and not self.status.get("detect", {}).get("customModel"):
            return [o for o in self._attr_options if o != "custom"]
        return self._attr_options

    @property
    def current_option(self) -> str | None:
        v = self.status.get("detect", {}).get(self._field)
        return v if v in self.options else None

    async def async_select_option(self, option: str) -> None:
        try:
            await self.coordinator.client.set_detect(**{self._field: option})
        except LynxError as err:
            raise HomeAssistantError(f"lynx rejected {self._field}: {err}") from err
        await self.coordinator.async_request_refresh()


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
