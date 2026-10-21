# SPDX-License-Identifier: MIT
"""Rest pose, sweep, and rotating the device token."""

from __future__ import annotations

from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.button import ButtonEntity, ButtonEntityDescription
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import LynxClient, LynxError
from .const import CONF_TOKEN
from .coordinator import LynxConfigEntry, LynxCoordinator
from .entity import LynxEntity

PARALLEL_UPDATES = 1


@dataclass(frozen=True, kw_only=True)
class LynxButtonDescription(ButtonEntityDescription):
    press: Callable[[LynxClient], Awaitable[dict[str, Any]]]


BUTTONS: tuple[LynxButtonDescription, ...] = (
    LynxButtonDescription(key="center", translation_key="center", press=lambda c: c.center()),
    LynxButtonDescription(key="sweep", translation_key="sweep", press=lambda c: c.sweep()),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: LynxConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities(
        [*(LynxButton(entry.runtime_data, d) for d in BUTTONS), LynxRotateTokenButton(entry.runtime_data)]
    )


class LynxButton(LynxEntity, ButtonEntity):
    entity_description: LynxButtonDescription

    def __init__(self, coordinator: LynxCoordinator, description: LynxButtonDescription) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description

    async def async_press(self) -> None:
        try:
            self.coordinator.apply(await self.entity_description.press(self.coordinator.client))
        except LynxError as err:
            raise HomeAssistantError(f"lynx did not respond: {err}") from err


class LynxRotateTokenButton(LynxEntity, ButtonEntity):
    """Mint a new device token and store it in the config entry, so Home Assistant keeps working.

    Anything else that used the old token (an MCP client, a script) needs the new one: it's on the
    device's serial console (`token`) and its setup page.
    """

    _attr_translation_key = "rotate_token"
    _attr_entity_category = EntityCategory.CONFIG

    def __init__(self, coordinator: LynxCoordinator) -> None:
        super().__init__(coordinator, "rotate_token")

    async def async_press(self) -> None:
        try:
            token = await self.coordinator.client.rotate_token()
        except LynxError as err:
            raise HomeAssistantError(f"lynx did not rotate its token: {err}") from err
        entry = self.coordinator.config_entry
        self.hass.config_entries.async_update_entry(entry, data={**entry.data, CONF_TOKEN: token})
