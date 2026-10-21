# SPDX-License-Identifier: MIT
"""The lynx pan/tilt camera."""

from __future__ import annotations

from homeassistant.components.camera import DOMAIN as CAMERA_DOMAIN
from homeassistant.const import CONF_HOST, Platform
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryAuthFailed, ConfigEntryNotReady
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers import service
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.typing import ConfigType

try:  # Home Assistant moved from voluptuous to probatio in 2026
    import probatio as vol
except ImportError:  # pragma: no cover
    import voluptuous as vol

from .api import LynxAuthError, LynxClient, LynxError
from .const import (
    ATTR_DURATION,
    ATTR_NAME,
    ATTR_PAN,
    ATTR_TILT,
    CONF_TOKEN,
    DOMAIN,
    SERVICE_DELETE_PRESET,
    SERVICE_LOOK,
    SERVICE_SAVE_PRESET,
)
from .coordinator import LynxConfigEntry, LynxCoordinator

PLATFORMS: list[Platform] = [
    Platform.BINARY_SENSOR,
    Platform.BUTTON,
    Platform.CAMERA,
    Platform.NUMBER,
    Platform.SELECT,
    Platform.SENSOR,
    Platform.SWITCH,
    Platform.UPDATE,
]

CONFIG_SCHEMA = cv.config_entry_only_config_schema(DOMAIN)

LOOK_SCHEMA = {
    vol.Optional(ATTR_PAN): vol.All(vol.Coerce(float), vol.Range(min=-90, max=90)),
    vol.Optional(ATTR_TILT): vol.All(vol.Coerce(float), vol.Range(min=-60, max=60)),
    vol.Optional(ATTR_DURATION): vol.All(vol.Coerce(int), vol.Range(min=0, max=10000)),
}
SAVE_PRESET_SCHEMA = {
    vol.Required(ATTR_NAME): vol.All(cv.string, vol.Length(min=1, max=15)),
    vol.Optional(ATTR_PAN): vol.All(vol.Coerce(float), vol.Range(min=-90, max=90)),
    vol.Optional(ATTR_TILT): vol.All(vol.Coerce(float), vol.Range(min=-60, max=60)),
}
DELETE_PRESET_SCHEMA = {vol.Required(ATTR_NAME): cv.string}
# (action, schema, camera method)
CAMERA_ACTIONS = (
    (SERVICE_LOOK, LOOK_SCHEMA, "async_look"),
    (SERVICE_SAVE_PRESET, SAVE_PRESET_SCHEMA, "async_save_preset"),
    (SERVICE_DELETE_PRESET, DELETE_PRESET_SCHEMA, "async_delete_preset"),
)


async def async_setup(hass: HomeAssistant, config: ConfigType) -> bool:
    """Register the lynx.look action (targets a lynx camera entity)."""
    if not hasattr(service, "async_register_platform_entity_service"):
        return True  # older Home Assistant: camera.py registers it per platform instead
    for name, schema, func in CAMERA_ACTIONS:
        service.async_register_platform_entity_service(
            hass, DOMAIN, name, entity_domain=CAMERA_DOMAIN, schema=schema, func=func
        )
    return True


async def async_setup_entry(hass: HomeAssistant, entry: LynxConfigEntry) -> bool:
    client = LynxClient(async_get_clientsession(hass), entry.data[CONF_HOST], entry.data[CONF_TOKEN])
    try:
        info = await client.info()
        await client.status()  # proves the token before any entity exists
    except LynxAuthError as err:
        raise ConfigEntryAuthFailed("the device token was rejected") from err
    except LynxError as err:
        raise ConfigEntryNotReady(str(err)) from err

    coordinator = LynxCoordinator(hass, entry, client, info)
    await coordinator.async_config_entry_first_refresh()
    entry.runtime_data = coordinator
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    # instant updates: the device pushes its status on change (cancelled with the entry)
    entry.async_create_background_task(hass, coordinator.listen(), f"{DOMAIN} events {entry.title}")
    return True


async def async_unload_entry(hass: HomeAssistant, entry: LynxConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
