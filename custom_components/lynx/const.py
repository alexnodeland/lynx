# SPDX-License-Identifier: MIT
"""Constants for the lynx integration."""

from datetime import timedelta
from typing import Final

DOMAIN: Final = "lynx"
MANUFACTURER: Final = "lynx open hardware"
MODEL: Final = "lynx pan/tilt camera"

CONF_TOKEN: Final = "token"

SCAN_INTERVAL: Final = timedelta(seconds=10)
STREAM_PORT: Final = 81

SERVICE_LOOK: Final = "look"
ATTR_PAN: Final = "pan"
ATTR_TILT: Final = "tilt"
ATTR_DURATION: Final = "duration"

FRAMESIZES: Final = ["QVGA", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA"]
