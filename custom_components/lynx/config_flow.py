# SPDX-License-Identifier: MIT
"""Config flow: found by zeroconf (_lynx._tcp) or added by address; the user supplies the token."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_HOST
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

try:
    import probatio as vol
except ImportError:  # pragma: no cover
    import voluptuous as vol

from .api import LynxAuthError, LynxClient, LynxConnectionError, LynxError
from .const import CONF_TOKEN, DOMAIN

TOKEN_HELP = "https://github.com/alexnodeland/lynx/tree/main/firmware/lynx#security-model"


class LynxConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._host: str | None = None
        self._name: str | None = None

    async def _validate(self, host: str, token: str) -> tuple[dict[str, Any] | None, str | None]:
        """Returns (info, None) on success or (None, error key)."""
        client = LynxClient(async_get_clientsession(self.hass), host, token)
        try:
            info = await client.info()
            await client.status()
        except LynxAuthError:
            return None, "invalid_auth"
        except LynxConnectionError:
            return None, "cannot_connect"
        except LynxError:
            return None, "unknown"
        if info.get("product") != "lynx" or not info.get("id"):
            return None, "not_lynx"
        return info, None

    # ---- manual ----

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            host = user_input[CONF_HOST].strip()
            info, err = await self._validate(host, user_input[CONF_TOKEN].strip())
            if info:
                await self.async_set_unique_id(info["id"])
                self._abort_if_unique_id_configured(updates={CONF_HOST: host})
                return self.async_create_entry(
                    title=info.get("name", "lynx"), data={CONF_HOST: host, CONF_TOKEN: user_input[CONF_TOKEN].strip()}
                )
            errors["base"] = err or "unknown"
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({vol.Required(CONF_HOST): str, vol.Required(CONF_TOKEN): str}),
            errors=errors,
            description_placeholders={"token_help": TOKEN_HELP},
        )

    # ---- discovered ----

    async def async_step_zeroconf(self, discovery_info: ZeroconfServiceInfo) -> ConfigFlowResult:
        dev_id = discovery_info.properties.get("id")
        if not dev_id:
            return self.async_abort(reason="not_lynx")
        self._host = str(discovery_info.ip_address)
        self._name = discovery_info.hostname.removesuffix(".").removesuffix(".local") or "lynx"
        await self.async_set_unique_id(dev_id)
        self._abort_if_unique_id_configured(updates={CONF_HOST: self._host})  # follows DHCP changes
        self.context["title_placeholders"] = {"name": self._name}
        return await self.async_step_zeroconf_confirm()

    async def async_step_zeroconf_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        assert self._host is not None
        errors: dict[str, str] = {}
        if user_input is not None:
            token = user_input[CONF_TOKEN].strip()
            info, err = await self._validate(self._host, token)
            if info:
                return self.async_create_entry(
                    title=info.get("name", self._name), data={CONF_HOST: self._host, CONF_TOKEN: token}
                )
            errors["base"] = err or "unknown"
        return self.async_show_form(
            step_id="zeroconf_confirm",
            data_schema=vol.Schema({vol.Required(CONF_TOKEN): str}),
            errors=errors,
            description_placeholders={"name": self._name or "lynx", "host": self._host, "token_help": TOKEN_HELP},
        )

    # ---- token rotated (serial `token new`) or wrong ----

    async def async_step_reauth(self, entry_data: Mapping[str, Any]) -> ConfigFlowResult:
        return await self.async_step_reauth_confirm()

    async def async_step_reauth_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        entry = self._get_reauth_entry()
        errors: dict[str, str] = {}
        if user_input is not None:
            token = user_input[CONF_TOKEN].strip()
            info, err = await self._validate(entry.data[CONF_HOST], token)
            if info:
                await self.async_set_unique_id(info["id"])
                self._abort_if_unique_id_mismatch(reason="wrong_device")
                return self.async_update_reload_and_abort(entry, data_updates={CONF_TOKEN: token})
            errors["base"] = err or "unknown"
        return self.async_show_form(
            step_id="reauth_confirm",
            data_schema=vol.Schema({vol.Required(CONF_TOKEN): str}),
            errors=errors,
            description_placeholders={"name": entry.title, "token_help": TOKEN_HELP},
        )

    # ---- moved to a new address ----

    async def async_step_reconfigure(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        entry = self._get_reconfigure_entry()
        errors: dict[str, str] = {}
        if user_input is not None:
            host = user_input[CONF_HOST].strip()
            info, err = await self._validate(host, entry.data[CONF_TOKEN])
            if info:
                await self.async_set_unique_id(info["id"])
                self._abort_if_unique_id_mismatch(reason="wrong_device")
                return self.async_update_reload_and_abort(entry, data_updates={CONF_HOST: host})
            errors["base"] = err or "unknown"
        return self.async_show_form(
            step_id="reconfigure",
            data_schema=vol.Schema({vol.Required(CONF_HOST, default=entry.data[CONF_HOST]): str}),
            errors=errors,
        )
