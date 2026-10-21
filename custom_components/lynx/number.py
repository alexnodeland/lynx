# SPDX-License-Identifier: MIT
"""Pan and tilt as sliders, plus camera brightness and JPEG quality."""

from __future__ import annotations

from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.number import NumberEntity, NumberEntityDescription, NumberMode
from homeassistant.const import DEGREE, EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import LynxClient, LynxError
from .coordinator import LynxConfigEntry, LynxCoordinator
from .entity import LynxEntity

PARALLEL_UPDATES = 1


@dataclass(frozen=True, kw_only=True)
class LynxNumberDescription(NumberEntityDescription):
    value: Callable[[dict[str, Any]], float | None]
    set: Callable[[LynxClient, float], Awaitable[dict[str, Any]]]
    limits: Callable[[dict[str, Any]], tuple[float, float]] | None = None  # live soft limits from the device


def _axis_limits(axis: str) -> Callable[[dict[str, Any]], tuple[float, float]]:
    return lambda s: (float(s[axis]["min"]), float(s[axis]["max"]))


NUMBERS: tuple[LynxNumberDescription, ...] = (
    LynxNumberDescription(
        key="pan",
        translation_key="pan",
        native_unit_of_measurement=DEGREE,
        native_min_value=-88,
        native_max_value=88,
        native_step=1,
        mode=NumberMode.SLIDER,
        value=lambda s: float(s["pan"]["target"]),
        set=lambda c, v: c.position(pan=v),
        limits=_axis_limits("pan"),
    ),
    LynxNumberDescription(
        key="tilt",
        translation_key="tilt",
        native_unit_of_measurement=DEGREE,
        native_min_value=-58,
        native_max_value=58,
        native_step=1,
        mode=NumberMode.SLIDER,
        value=lambda s: float(s["tilt"]["target"]),
        set=lambda c, v: c.position(tilt=v),
        limits=_axis_limits("tilt"),
    ),
    LynxNumberDescription(
        key="brightness",
        translation_key="brightness",
        entity_category=EntityCategory.CONFIG,
        native_min_value=-2,
        native_max_value=2,
        native_step=1,
        value=lambda s: s["camera"]["brightness"],
        set=lambda c, v: c.set_camera(brightness=int(v)),
    ),
    LynxNumberDescription(
        key="quality",
        translation_key="quality",
        entity_category=EntityCategory.CONFIG,
        entity_registry_enabled_default=False,
        native_min_value=4,
        native_max_value=63,
        native_step=1,
        value=lambda s: s["camera"]["quality"],
        set=lambda c, v: c.set_camera(quality=int(v)),
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: LynxConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities(LynxNumber(entry.runtime_data, d) for d in NUMBERS)


class LynxNumber(LynxEntity, NumberEntity):
    entity_description: LynxNumberDescription

    def __init__(self, coordinator: LynxCoordinator, description: LynxNumberDescription) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> float | None:
        try:
            return self.entity_description.value(self.status)
        except (KeyError, TypeError):
            return None

    @property
    def native_min_value(self) -> float:
        if self.entity_description.limits:
            try:
                return self.entity_description.limits(self.status)[0]
            except (KeyError, TypeError, ValueError):
                pass
        return self.entity_description.native_min_value

    @property
    def native_max_value(self) -> float:
        if self.entity_description.limits:
            try:
                return self.entity_description.limits(self.status)[1]
            except (KeyError, TypeError, ValueError):
                pass
        return self.entity_description.native_max_value

    async def async_set_native_value(self, value: float) -> None:
        try:
            result = await self.entity_description.set(self.coordinator.client, value)
        except LynxError as err:
            raise HomeAssistantError(f"lynx rejected {self.entity_description.key}: {err}") from err
        if "pan" in result:
            self.coordinator.apply(result)
        else:
            await self.coordinator.async_request_refresh()
