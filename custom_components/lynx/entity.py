# SPDX-License-Identifier: MIT
"""Base entity: one device per lynx."""

from __future__ import annotations

from homeassistant.helpers.device_registry import CONNECTION_NETWORK_MAC, DeviceInfo, format_mac
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN, MANUFACTURER, MODEL
from .coordinator import LynxCoordinator


class LynxEntity(CoordinatorEntity[LynxCoordinator]):
    _attr_has_entity_name = True

    def __init__(self, coordinator: LynxCoordinator, key: str) -> None:
        super().__init__(coordinator)
        info = coordinator.info
        dev_id = info["id"]
        self._attr_unique_id = f"{dev_id}-{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, dev_id)},
            connections={(CONNECTION_NETWORK_MAC, format_mac(dev_id))},
            name=info.get("name", "lynx"),
            manufacturer=MANUFACTURER,
            model=MODEL,
            sw_version=(coordinator.data or {}).get("version") or info.get("version"),
            configuration_url=coordinator.client.base_url + "/",
        )

    @property
    def status(self) -> dict:
        return self.coordinator.data or {}
