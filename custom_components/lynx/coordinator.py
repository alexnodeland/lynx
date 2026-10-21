# SPDX-License-Identifier: MIT
"""Polls a lynx's status."""

from __future__ import annotations

import logging
from typing import Any

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryAuthFailed
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import LynxAuthError, LynxClient, LynxError
from .const import DOMAIN, SCAN_INTERVAL

_LOGGER = logging.getLogger(__name__)

type LynxConfigEntry = ConfigEntry[LynxCoordinator]


class LynxCoordinator(DataUpdateCoordinator[dict[str, Any]]):
    """One coordinator per device; entities read `data` (the device's /api/status)."""

    config_entry: LynxConfigEntry

    def __init__(self, hass: HomeAssistant, entry: LynxConfigEntry, client: LynxClient, info: dict[str, Any]) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name=DOMAIN, update_interval=SCAN_INTERVAL)
        self.client = client
        self.info = info  # /api/info at setup: id, name, version

    async def _async_update_data(self) -> dict[str, Any]:
        try:
            return await self.client.status()
        except LynxAuthError as err:
            raise ConfigEntryAuthFailed("the device token was rejected") from err
        except LynxError as err:
            raise UpdateFailed(str(err)) from err

    def apply(self, status: dict[str, Any]) -> None:
        """Commands answer with fresh status; use it instead of waiting for the next poll."""
        if isinstance(status, dict) and "pan" in status:
            self.async_set_updated_data(status)
