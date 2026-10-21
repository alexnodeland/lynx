# SPDX-License-Identifier: MIT
"""Whether the head is moving."""

from __future__ import annotations

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import LynxConfigEntry, LynxCoordinator
from .entity import LynxEntity

PARALLEL_UPDATES = 0


async def async_setup_entry(
    hass: HomeAssistant, entry: LynxConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([LynxMoving(entry.runtime_data)])


class LynxMoving(LynxEntity, BinarySensorEntity):
    _attr_translation_key = "moving"
    _attr_device_class = BinarySensorDeviceClass.MOVING

    def __init__(self, coordinator: LynxCoordinator) -> None:
        super().__init__(coordinator, "moving")

    @property
    def is_on(self) -> bool | None:
        s = self.status
        if "pan" not in s:
            return None
        return bool(s["pan"].get("moving") or s["tilt"].get("moving") or s.get("sweeping"))
