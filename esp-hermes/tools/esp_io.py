"""
esp_io.py — ESP-Hermes IO-tool handlers (gateway side)
=======================================================

These handlers back the IO-tool layer (implementation-spec §3.4). Hermes sees
them as normal tools; this module serializes the call to the connected ESP via
the WS hub (gateway/esp_hermes_ws.py) and returns the device's reply.

Supported tools (spec §3.4):
    esp_gpio_set   { pin, state: "HIGH"|"LOW" }
    esp_gpio_read  { pin }                       -> level
    esp_adc_read   { pin, atten }               -> mV
    esp_pwm_set    { pin, duty, freq }          -> ledc
    esp_i2c_read   { addr, reg, len }           -> bytes
    esp_i2c_write  { addr, reg, data: [] }
    esp_uart_send  { data }                      -> Serial passthrough
    esp_imu_read   { axis: "all"|"accel"|"gyro" } -> vector
    esp_motor_set  { angle|speed }              # Phase 5 (motor attached)

Safety (spec §8 / §17) is enforced UPSTREAM of these handlers — the gateway
adapter must consult the per-device allowlist and approval gate before calling
dispatch(), so this module only performs the transport.

NOTE: scaffold. The body of `dispatch` forwards to the hub and returns the
device result; fine-grained validation/allowlist lives in the adapter.
"""

from __future__ import annotations

import logging
from typing import Any, Awaitable, Callable, Dict, Optional

logger = logging.getLogger("esp_hermes.tools")

# Stable tool schema — mirrors spec §3.4. Used for registration + docs.
TOOL_SCHEMAS: Dict[str, Dict[str, Any]] = {
    "esp_gpio_set":  {"params": {"pin": "int", "state": "HIGH|LOW"}},
    "esp_gpio_read": {"params": {"pin": "int"}, "returns": "level"},
    "esp_adc_read":   {"params": {"pin": "int", "atten": "int"}, "returns": "mV"},
    "esp_pwm_set":    {"params": {"pin": "int", "duty": "int", "freq": "int"}},
    "esp_i2c_read":   {"params": {"addr": "int", "reg": "int", "len": "int"},
                       "returns": "bytes"},
    "esp_i2c_write":  {"params": {"addr": "int", "reg": "int", "data": "list[int]"}},
    "esp_uart_send":  {"params": {"data": "str"}},
    "esp_imu_read":   {"params": {"axis": "all|accel|gyro"}, "returns": "vector"},
    "esp_motor_set":  {"params": {"angle": "int", "speed": "int"}, "phase": 5},
}

# The hub instance is injected by the adapter at startup.
_hub: Optional[Any] = None
ToolDispatcher = Callable[[str, str, Dict[str, Any], str], Awaitable[Dict[str, Any]]]


def bind_hub(hub: Any) -> None:
    """Wire this module to the live WSHub (called from the gateway adapter)."""
    global _hub
    _hub = hub


async def dispatch(device_id: str, tool: str, params: Dict[str, Any],
                   call_id: str) -> Dict[str, Any]:
    """
    ToolDispatcher entrypoint registered on WSHub.set_tool_dispatcher.

    Forwards the call to the physical device and returns its tool_result:
        {"ok": bool, "value": Any, "error": str|None}

    If no hub is bound (unit test / offline), returns a clear error rather than
    silently dropping the call.
    """
    if _hub is None:
        return {"ok": False, "value": None, "error": "hub_not_bound"}
    if tool not in TOOL_SCHEMAS:
        return {"ok": False, "value": None, "error": f"unknown_tool:{tool}"}
    # Forward to the device over the WS hub (spec §3.3 tool_call -> §3.2 result)
    result = await _hub.call_tool(device_id, tool, params, timeout=5.0)
    logger.info("io tool %s dev=%s -> %s", tool, device_id, result.get("ok"))
    return result
