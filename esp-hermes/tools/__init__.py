"""esp-hermes IO-tool handlers (gateway side).

Mirrors implementation-spec §3.4. The actual device round-trip lives in
gateway/esp_hermes_ws.py (WSHub.call_tool). This module registers the
tool handlers and wires them to the hub's ToolDispatcher so a tool_call
from Hermes routes to the physical ESP correctly.

Status: scaffold. Handler bodies are intentionally minimal until the
gateway adapter (gateway/esp_hermes.py) and a live device exercise them.
"""
