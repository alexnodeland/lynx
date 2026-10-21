# SPDX-License-Identifier: MIT
"""A small async client for the lynx device's HTTP API."""

from __future__ import annotations

import asyncio
from typing import Any

import aiohttp

from .const import STREAM_PORT

TIMEOUT = 10


class LynxError(Exception):
    """Base error talking to a lynx."""


class LynxConnectionError(LynxError):
    """The device could not be reached."""


class LynxAuthError(LynxError):
    """The device rejected the token."""


class LynxClient:
    """Talks to one lynx. The token always goes in the Authorization header, never in a URL."""

    def __init__(self, session: aiohttp.ClientSession, host: str, token: str) -> None:
        self._session = session
        self.host = host
        self._token = token

    @property
    def base_url(self) -> str:
        host = f"[{self.host}]" if ":" in self.host else self.host
        return f"http://{host}"

    @property
    def stream_url(self) -> str:
        host = f"[{self.host}]" if ":" in self.host else self.host
        return f"http://{host}:{STREAM_PORT}/stream"

    @property
    def auth_headers(self) -> dict[str, str]:
        return {"Authorization": f"Bearer {self._token}"}

    async def _request(self, method: str, path: str, *, auth: bool = True, json: Any = None) -> aiohttp.ClientResponse:
        try:
            async with asyncio.timeout(TIMEOUT):
                resp = await self._session.request(
                    method, f"{self.base_url}{path}", headers=self.auth_headers if auth else None, json=json
                )
        except (TimeoutError, aiohttp.ClientError) as err:
            raise LynxConnectionError(f"cannot reach {self.host}: {err}") from err
        if resp.status == 401:
            resp.release()
            raise LynxAuthError("token rejected")
        if resp.status >= 400:
            text = await resp.text()
            raise LynxError(f"{method} {path} -> {resp.status}: {text[:200]}")
        return resp

    async def _json(self, method: str, path: str, *, auth: bool = True, json: Any = None) -> dict[str, Any]:
        resp = await self._request(method, path, auth=auth, json=json)
        try:
            return await resp.json(content_type=None)
        except (aiohttp.ContentTypeError, ValueError) as err:
            raise LynxError(f"bad JSON from {path}") from err

    async def info(self) -> dict[str, Any]:
        """Name, id, version. Needs no token."""
        return await self._json("GET", "/api/info", auth=False)

    async def status(self) -> dict[str, Any]:
        return await self._json("GET", "/api/status")

    async def position(
        self, pan: float | None = None, tilt: float | None = None, ms: int | None = None
    ) -> dict[str, Any]:
        body: dict[str, Any] = {}
        if pan is not None:
            body["pan"] = pan
        if tilt is not None:
            body["tilt"] = tilt
        if ms is not None:
            body["ms"] = ms
        return await self._json("POST", "/api/position", json=body)

    async def center(self) -> dict[str, Any]:
        return await self._json("POST", "/api/center")

    async def sweep(self) -> dict[str, Any]:
        return await self._json("POST", "/api/sweep")

    async def set_camera(self, **settings: Any) -> dict[str, Any]:
        return await self._json("POST", "/api/camera", json=settings)

    async def snapshot(self) -> bytes:
        resp = await self._request("GET", "/snapshot.jpg")
        return await resp.read()
