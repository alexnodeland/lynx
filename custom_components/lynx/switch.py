# SPDX-License-Identifier: MIT
"""Image flip and mirror (for however the board sits in the pod)."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from homeassistant.components.switch import SwitchEntity, SwitchEntityDescription
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import LynxError
from .coordinator import LynxConfigEntry, LynxCoordinator
from .entity import LynxEntity

PARALLEL_UPDATES = 1


@dataclass(frozen=True, kw_only=True)
class LynxSwitchDescription(SwitchEntityDescription):
    field: str


SWITCHES: tuple[LynxSwitchDescription, ...] = (
    LynxSwitchDescription(key="vflip", translation_key="vflip", field="vflip", entity_category=EntityCategory.CONFIG),
    LynxSwitchDescription(
        key="hmirror", translation_key="hmirror", field="hmirror", entity_category=EntityCategory.CONFIG
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: LynxConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities(LynxSwitch(entry.runtime_data, d) for d in SWITCHES)


class LynxSwitch(LynxEntity, SwitchEntity):
    entity_description: LynxSwitchDescription

    def __init__(self, coordinator: LynxCoordinator, description: LynxSwitchDescription) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def is_on(self) -> bool | None:
        return self.status.get("camera", {}).get(self.entity_description.field)

    async def _set(self, on: bool) -> None:
        try:
            await self.coordinator.client.set_camera(**{self.entity_description.field: on})
        except LynxError as err:
            raise HomeAssistantError(f"lynx rejected {self.entity_description.field}: {err}") from err
        await self.coordinator.async_request_refresh()

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self._set(True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self._set(False)
