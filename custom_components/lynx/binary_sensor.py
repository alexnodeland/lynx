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
    async_add_entities([LynxMoving(entry.runtime_data), LynxDetected(entry.runtime_data)])


class LynxDetected(LynxEntity, BinarySensorEntity):
    """Something (a face, or a cat's face) seen in the last 10 s, by the on-device detector."""

    _attr_translation_key = "detected"
    _attr_device_class = BinarySensorDeviceClass.OCCUPANCY

    def __init__(self, coordinator: LynxCoordinator) -> None:
        super().__init__(coordinator, "detected")

    @property
    def available(self) -> bool:
        return super().available and bool(self.status.get("detect", {}).get("enabled"))

    @property
    def is_on(self) -> bool | None:
        return bool(self.status.get("detect", {}).get("present"))

    @property
    def extra_state_attributes(self) -> dict:
        d = self.status.get("detect", {})
        return {"model": d.get("model"), "count": d.get("count"), "tracking": d.get("tracking")}


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
