<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
# Security policy

## Reporting a vulnerability

Please report it privately through GitHub's [private vulnerability reporting](https://github.com/alexnodeland/lynx/security/advisories/new),
not in a public issue. Include the firmware version (`GET /api/info`), what you did, and what happened.
You'll get a reply within a week.

## Supported versions

Fixes go into the latest release. Updating is one click from the dashboard or Home Assistant, and the
device rolls back by itself if an update fails.

## Threat model

What lynx protects, and what it doesn't (no TLS, physical access is full access): [docs/security.md](docs/security.md).
