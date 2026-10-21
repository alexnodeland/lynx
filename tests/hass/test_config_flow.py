# SPDX-License-Identifier: MIT
"""Config flow: manual, zeroconf discovery, reauth, reconfigure."""

from __future__ import annotations

from ipaddress import ip_address

import pytest
from homeassistant import config_entries
from homeassistant.const import CONF_HOST
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from custom_components.lynx.const import CONF_TOKEN, DOMAIN

from .conftest import BASE, DEV_ID, HOST, INFO, TOKEN


@pytest.fixture
def expected_lingering_timers() -> bool:
    """Flows that change an entry's data (reauth, reconfigure, a discovered new IP) make Home Assistant
    schedule a delayed write of the config-entry store; that timer may outlive the test by design."""
    return True


def zc(props: dict | None = None, host: str = HOST) -> ZeroconfServiceInfo:
    return ZeroconfServiceInfo(
        ip_address=ip_address(host),
        ip_addresses=[ip_address(host)],
        port=80,
        hostname="lynx-42e0.local.",
        type="_lynx._tcp.local.",
        name="lynx-42e0._lynx._tcp.local.",
        properties={"id": DEV_ID, "version": "0.3.0", "mcp": "/mcp"} if props is None else props,
    )


async def test_user_flow(hass, lynx):
    r = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})
    assert r["type"] is FlowResultType.FORM
    r = await hass.config_entries.flow.async_configure(r["flow_id"], {CONF_HOST: HOST, CONF_TOKEN: f" {TOKEN} "})
    assert r["type"] is FlowResultType.CREATE_ENTRY
    assert r["title"] == "lynx-42e0"
    assert r["data"] == {CONF_HOST: HOST, CONF_TOKEN: TOKEN}
    assert r["result"].unique_id == DEV_ID
    # the token went in the header, never the URL
    status_calls = [c for c in lynx.mock_calls if str(c[1]).endswith("/api/status")]
    assert status_calls and all("token=" not in str(c[1]) for c in lynx.mock_calls)


async def test_user_flow_bad_token_then_good(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/info", json=INFO)
    aioclient_mock.get(f"{BASE}/api/status", status=401)
    r = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})
    r = await hass.config_entries.flow.async_configure(r["flow_id"], {CONF_HOST: HOST, CONF_TOKEN: "nope"})
    assert r["type"] is FlowResultType.FORM
    assert r["errors"] == {"base": "invalid_auth"}


async def test_user_flow_cannot_connect(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/info", exc=TimeoutError)
    r = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})
    r = await hass.config_entries.flow.async_configure(r["flow_id"], {CONF_HOST: HOST, CONF_TOKEN: TOKEN})
    assert r["errors"] == {"base": "cannot_connect"}


async def test_user_flow_not_a_lynx(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/info", json={"name": "printer"})
    aioclient_mock.get(f"{BASE}/api/status", json={})
    r = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})
    r = await hass.config_entries.flow.async_configure(r["flow_id"], {CONF_HOST: HOST, CONF_TOKEN: TOKEN})
    assert r["errors"] == {"base": "not_lynx"}


async def test_zeroconf_flow(hass, lynx):
    r = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=zc())
    assert r["type"] is FlowResultType.FORM
    assert r["step_id"] == "zeroconf_confirm"
    assert r["description_placeholders"]["name"] == "lynx-42e0"
    r = await hass.config_entries.flow.async_configure(r["flow_id"], {CONF_TOKEN: TOKEN})
    assert r["type"] is FlowResultType.CREATE_ENTRY
    assert r["result"].unique_id == DEV_ID
    assert r["data"][CONF_HOST] == HOST


async def test_zeroconf_updates_host_of_known_device(hass, entry):
    r = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=zc(host="192.168.1.77")
    )
    assert r["type"] is FlowResultType.ABORT
    assert r["reason"] == "already_configured"
    assert entry.data[CONF_HOST] == "192.168.1.77"  # DHCP moved it; HA follows


async def test_zeroconf_without_id_aborts(hass):
    r = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_ZEROCONF}, data=zc(props={})
    )
    assert r["type"] is FlowResultType.ABORT
    assert r["reason"] == "not_lynx"


async def test_reauth(hass, entry, lynx):
    r = await entry.start_reauth_flow(hass)
    assert r["step_id"] == "reauth_confirm"
    r = await hass.config_entries.flow.async_configure(r["flow_id"], {CONF_TOKEN: "a" * 32})
    assert r["type"] is FlowResultType.ABORT
    assert r["reason"] == "reauth_successful"
    assert entry.data[CONF_TOKEN] == "a" * 32


async def test_reconfigure_new_address(hass, entry, aioclient_mock, status):
    new = "http://192.168.1.99"
    aioclient_mock.get(f"{new}/api/info", json=INFO)
    aioclient_mock.get(f"{new}/api/status", json=status)  # the entry reloads against the new address
    r = await entry.start_reconfigure_flow(hass)
    r = await hass.config_entries.flow.async_configure(r["flow_id"], {CONF_HOST: "192.168.1.99"})
    await hass.async_block_till_done()
    assert r["reason"] == "reconfigure_successful"
    assert entry.data[CONF_HOST] == "192.168.1.99"


async def test_reconfigure_rejects_another_device(hass, entry, aioclient_mock):
    new = "http://192.168.1.98"
    aioclient_mock.get(f"{new}/api/info", json={**INFO, "id": "aaaaaaaaaaaa"})
    aioclient_mock.get(f"{new}/api/status", json={})
    r = await entry.start_reconfigure_flow(hass)
    r = await hass.config_entries.flow.async_configure(r["flow_id"], {CONF_HOST: "192.168.1.98"})
    assert r["reason"] == "wrong_device"
    assert entry.data[CONF_HOST] == HOST
