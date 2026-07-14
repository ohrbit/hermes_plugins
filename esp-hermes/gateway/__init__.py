"""ESP-Hermes gateway-side package.

Exposes the WebSocket bridge + pet-state hub and IO-tool helpers so the
Hermes gateway can import them as ``hermes_plugins.esp_hermes.gateway``.
"""

from .esp_hermes_ws import (
    WSHub,
    Client,
    connect_ws,
    HEARTBEAT_INTERVAL_S,
    HEARTBEAT_TIMEOUT_S,
    INBOUND_TYPES,
    OUTBOUND_TYPES,
)

__all__ = [
    "WSHub",
    "Client",
    "connect_ws",
    "HEARTBEAT_INTERVAL_S",
    "HEARTBEAT_TIMEOUT_S",
    "INBOUND_TYPES",
    "OUTBOUND_TYPES",
]
