# SPDX-License-Identifier: MIT
"""Rest pose and sweep."""

from __future__ import annotations

from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.button import ButtonEntity, ButtonEntityDescription
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import LynxClient, LynxError
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
    async_add_entities(LynxButton(entry.runtime_data, d) for d in BUTTONS)


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
