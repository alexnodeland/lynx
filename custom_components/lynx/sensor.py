# SPDX-License-Identifier: MIT
"""Diagnostics as sensors: Wi-Fi signal, boot time, memory."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Any

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.const import SIGNAL_STRENGTH_DECIBELS_MILLIWATT, EntityCategory, UnitOfInformation
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.util import dt as dt_util

from .coordinator import LynxConfigEntry, LynxCoordinator
from .entity import LynxEntity

PARALLEL_UPDATES = 0


@dataclass(frozen=True, kw_only=True)
class LynxSensorDescription(SensorEntityDescription):
    value: Callable[[dict[str, Any]], Any]


SENSORS: tuple[LynxSensorDescription, ...] = (
    LynxSensorDescription(
        key="rssi",
        translation_key="rssi",
        device_class=SensorDeviceClass.SIGNAL_STRENGTH,
        native_unit_of_measurement=SIGNAL_STRENGTH_DECIBELS_MILLIWATT,
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        value=lambda s: s["network"]["rssi"] or None,
    ),
    LynxSensorDescription(
        key="detected_count",
        translation_key="detected_count",
        state_class=SensorStateClass.MEASUREMENT,
        value=lambda s: s["detect"]["count"] if s["detect"]["enabled"] else None,
    ),
    LynxSensorDescription(
        key="free_heap",
        translation_key="free_heap",
        device_class=SensorDeviceClass.DATA_SIZE,
        native_unit_of_measurement=UnitOfInformation.BYTES,
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        entity_registry_enabled_default=False,
        value=lambda s: s["freeHeap"],
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: LynxConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    async_add_entities([*(LynxSensor(coordinator, d) for d in SENSORS), LynxBootTime(coordinator)])


class LynxSensor(LynxEntity, SensorEntity):
    entity_description: LynxSensorDescription

    def __init__(self, coordinator: LynxCoordinator, description: LynxSensorDescription) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> Any:
        try:
            return self.entity_description.value(self.status)
        except (KeyError, TypeError):
            return None


class LynxBootTime(LynxEntity, SensorEntity):
    """When the device last started (a reset or power loss shows up as a jump)."""

    _attr_translation_key = "last_boot"
    _attr_device_class = SensorDeviceClass.TIMESTAMP
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator: LynxCoordinator) -> None:
        super().__init__(coordinator, "last_boot")
        self._boot: datetime | None = None

    @property
    def native_value(self) -> datetime | None:
        up = self.status.get("uptimeMs")
        if up is None:
            return self._boot
        boot = dt_util.utcnow() - timedelta(milliseconds=up)
        # polling jitter moves the estimate by a few ms; only a real restart should change the state
        if self._boot is None or abs((boot - self._boot).total_seconds()) > 60:
            self._boot = boot.replace(microsecond=0)
        return self._boot
