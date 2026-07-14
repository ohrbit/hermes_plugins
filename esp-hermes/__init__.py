"""
esp-hermes — Hermes gateway platform plugin (user plugin root).

This package is discovered by the Hermes plugin manager when placed under
``~/.hermes/plugins/esp-hermes/`` (or in the ``ohrbit/hermes_plugins`` repo).
The plugin manager imports this module and calls :func:`register` to wire the
``esp_hermes`` platform adapter into the gateway.

The gateway-side code lives in the ``gateway`` subpackage:
  * ``gateway/esp_hermes.py``  — BasePlatformAdapter subclass (the adapter)
  * ``gateway/esp_hermes_ws.py`` — WebSocket bridge + pet-state hub
"""

from .gateway.esp_hermes import EspHermesAdapter, register
from .gateway.esp_hermes_ws import (
    WSHub,
    Client,
    connect_ws,
    HEARTBEAT_INTERVAL_S,
    HEARTBEAT_TIMEOUT_S,
    INBOUND_TYPES,
    OUTBOUND_TYPES,
)

__all__ = [
    "EspHermesAdapter",
    "register",
    "WSHub",
    "Client",
    "connect_ws",
    "HEARTBEAT_INTERVAL_S",
    "HEARTBEAT_TIMEOUT_S",
    "INBOUND_TYPES",
    "OUTBOUND_TYPES",
]
