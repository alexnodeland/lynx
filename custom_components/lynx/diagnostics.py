# SPDX-License-Identifier: MIT
"""Diagnostics download: the device's status and info, token redacted."""

from __future__ import annotations

from typing import Any

from homeassistant.components.diagnostics import async_redact_data
from homeassistant.core import HomeAssistant

from .const import CONF_TOKEN
from .coordinator import LynxConfigEntry


async def async_get_config_entry_diagnostics(hass: HomeAssistant, entry: LynxConfigEntry) -> dict[str, Any]:
    coordinator = entry.runtime_data
    return {
        "entry": async_redact_data(dict(entry.data), {CONF_TOKEN}),
        "info": coordinator.info,
        "status": coordinator.data,
    }
