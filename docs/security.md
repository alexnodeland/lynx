<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
# Security and threat model

lynx is a camera that can move, in someone's home. This page says what it protects, against whom, and
what it deliberately doesn't. To report a vulnerability, see [SECURITY.md](../SECURITY.md).

## What's worth protecting

| Asset | Why it matters |
|---|---|
| The picture | a live view into a home: the stream, snapshots, recordings on the microSD card |
| The head | someone who can move it can point the camera where you didn't |
| The token | it unlocks both of the above |
| The firmware | whoever can flash it owns the device |
| Your Wi-Fi password | stored on the device, in NVS |

## Who it defends against

| Attacker | Defended? | How |
|---|---|---|
| A device or person on your LAN without the token | **yes** | every endpoint but `/`, `/lynx.css` and `/api/info` needs the token; 128 random bits, constant-time compare |
| Something guessing tokens | **yes** | 2¹²⁸ tokens, plus a per-address throttle: 5 misses, then `429` for 1 s doubling to 5 min |
| A web page you visit (DNS rebinding, CSRF) | **yes** | the token is never in a cookie, so a page can't ride your session; `/mcp` also rejects a browser `Origin` that isn't the device |
| A bad firmware update | **yes** | an update must be an ESP32-S3 app image, can carry a SHA-256 the device checks, and rolls back by itself if it doesn't run healthily for 45 s |
| Someone in radio range during setup | partly | the setup hotspot is open and shows the token, but it's up only on first boot, after BOOT is held for 3 s, or while the saved network is unreachable |
| Someone sniffing your LAN | **no** | there's no TLS (an ESP32 serving HTTPS with a self-signed certificate helps little and costs a lot). The token crosses the LAN in the clear, as with most LAN cameras |
| The internet | out of scope | don't port-forward lynx. For remote access use a VPN (Tailscale, WireGuard) |
| Someone holding the device | **no** | USB gives the serial console (which prints the token) and flashing. Physical access is full access |

## What the AI-agent interface adds

MCP gives an agent the same powers as the REST API: move, look, detect, presets, patrol, recordings. lynx
narrows what a well-behaved agent does with them:

- Every tool carries `readOnlyHint` / `destructiveHint` / `idempotentHint`, so a client can ask you before
  anything that changes state.
- The instructions and the guide tell the agent to ask before turning on follow, patrol or recording (they keep
  running after the conversation), and not to try to identify people.
- Detection is detection, not recognition: the models find *a* face, never *whose*.

An agent holding the token still has full control. Give a token only to an agent you'd let move the camera.
Rotate it when you stop trusting one.

## Rotating the token

- **Home Assistant:** press *Rotate token* on the device page. Home Assistant stores the new token itself.
- **API:** `POST /api/token/rotate` with the current token returns the new one.
- **Serial:** `token new`.

The old token stops working at once. Anything else using it (an MCP client, a script) needs the new one.
Streams already open keep running until they close.

## What stays on the device

Frames, detections and recordings stay on the camera unless a client with the token asks for them. lynx
makes no outbound connections except NTP (for recording timestamps) and the GitHub release check that Home
Assistant, not the device, performs.
