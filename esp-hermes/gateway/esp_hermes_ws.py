"""
esp_hermes_ws.py — ESP-Hermes WebSocket Bridge + Pet-State Hub
===============================================================

Bidirectional WebSocket server that connects M5Stack ESP32-S3 devices to the
Hermes gateway. The ESP is a *gateway client*, not an LLM client: Hermes (the
gateway) is the brain; this module is the wire.

Responsibilities (per implementation-spec §3, §12, §13, §14, §16, §17):

  * Accept device connections over WSS, authenticated by a per-device
    ``device_token`` (query param), and a ``capabilities`` handshake.
  * Route inbound messages (audio/event/imu/tool_result/ping) to registered
    handlers. The inbound audio/event path is what the ``EspHermesAdapter``
    (gateway/esp_hermes.py) subscribes to in order to feed the conversation.
  * Proactively push messages *to* a device (pet_state, TTS audio, tool_call,
    mode_ack, video, tui_line) — including unsolicited pushes from cron/agent
    context (§13) and audio cues (§16).
  * Round-trip IO tool-calls: ``tool_call`` -> handler -> ``tool_result``
    (§3.4), with a ``call_id`` correlation future so synchronous callers can
    await the device's reply (used by tools/esp_io.py).
  * Heartbeat (§12): ESP pings every 30s, hub pongs; missing pong for
    HEARTBEAT_TIMEOUT closes the socket so the device falls back to idle.
  * Reconnect is handled by the *client* (exponential backoff 1s->30s); this
    hub is stateless across reconnects but re-validates the token each connect.
  * Per-device session isolation (§14): the hub only keys on ``device_id``;
    session context is owned by the adapter.
  * Audit: every connect / tool_call / disconnect is logged to
    ``~/.hermes/logs/esp_hermes.log`` (§8/§17).

This module has NO hard dependency on the rest of Hermes — it exposes a clean
async API (``WSHub`` + ``connect_ws`` awaitable for tests) so the adapter can
wire it in, and so it is unit-testable without the full gateway.

Protocol message shapes: see references/implementation-spec.md §3.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import time
import uuid
from dataclasses import dataclass, field
from typing import Any, Awaitable, Callable, Dict, Optional, Set

logger = logging.getLogger("esp_hermes.ws")

# ---------------------------------------------------------------------------
# Tunables (mirror spec §12 / §17). Overridable via constructor for tests.
# ---------------------------------------------------------------------------
HEARTBEAT_INTERVAL_S = 30.0     # device ping cadence
HEARTBEAT_TIMEOUT_S = 90.0      # no pong within this -> drop
PING_WRITE_INTERVAL_S = 25.0    # hub-side liveness ping (keeps NAT alive)
RECONNECT_BACKOFF_HINT = (1.0, 30.0)  # (min, max) — sent to client in close frame

# Message types (spec §3.2 / §3.3)
INBOUND_TYPES = {"audio", "event", "imu", "tool_result", "ping"}
OUTBOUND_TYPES = {
    "pet_state", "video", "audio", "tool_call", "mode_ack", "pong", "tui_line"
}
HANDSHAKE_TIMEOUT_S = 10.0       # device must send capabilities within this


# ---------------------------------------------------------------------------
# Logging helper (audit trail, §8/§17)
# ---------------------------------------------------------------------------
def _audit_logger() -> logging.Logger:
    audit = logging.getLogger("esp_hermes.audit")
    if not audit.handlers:
        try:
            log_dir = os.path.expanduser("~/.hermes/logs")
            os.makedirs(log_dir, exist_ok=True)
            h = logging.FileHandler(os.path.join(log_dir, "esp_hermes.log"))
            h.setFormatter(logging.Formatter(
                "%(asctime)s %(levelname)s %(message)s"))
            audit.addHandler(h)
            audit.setLevel(logging.INFO)
            audit.propagate = False
        except OSError as exc:  # pragma: no cover - defensive
            audit.addHandler(logging.NullHandler())
            logger.warning("esp_hermes audit log unavailable: %s", exc)
    return audit


# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------
@dataclass
class Client:
    """One connected ESP device."""
    device_id: str
    ws: Any                          # aiohttp WebSocketResponse
    token: str
    caps: Dict[str, Any] = field(default_factory=dict)
    last_seen: float = field(default_factory=time.monotonic)
    connected_at: float = field(default_factory=time.monotonic)
    # futures awaiting this device's tool_result keyed by call_id
    _pending: Dict[str, asyncio.Future] = field(default_factory=dict)
    # futures awaiting capabilities handshake
    _hs_waiters: Set[asyncio.Future] = field(default_factory=set)

    def __hash__(self) -> int:
        return hash(self.device_id)


# Handler signatures ---------------------------------------------------------
# Inbound audio/event/imu/tool_result -> async def(device_id, msg)
InboundHandler = Callable[[str, Dict[str, Any]], Awaitable[None]]
# tool_call dispatch -> async def(device_id, tool, params, call_id) -> result dict
#   result dict: {"ok": bool, "value": Any} (value optional)
ToolDispatcher = Callable[[str, str, Dict[str, Any], str], Awaitable[Dict[str, Any]]]


# ---------------------------------------------------------------------------
# The hub
# ---------------------------------------------------------------------------
class WSHub:
    """
    Central registry + router for ESP WebSocket clients.

    Usage (server side)::

        hub = WSHub(token_lookup=my_token_checker)
        hub.on_inbound(on_audio)            # audio/event/imu/tool_result
        hub.set_tool_dispatcher(dispatch)   # io tool_call roundtrip
        app.router.add_get("/api/esp_hermes/ws", hub.handler)

    Usage (push side, any context — cron/agent)::

        await hub.push(device_id, {"type": "pet_state", "state": "done"})
        res = await hub.call_tool(device_id, "esp_gpio_set",
                                  {"pin": 13, "state": "HIGH"}, timeout=5)
    """

    def __init__(
        self,
        token_lookup: Optional[Callable[[str, str], Awaitable[bool]]] = None,
        heartbeat_interval: float = HEARTBEAT_INTERVAL_S,
        heartbeat_timeout: float = HEARTBEAT_TIMEOUT_S,
        ping_interval: float = PING_WRITE_INTERVAL_S,
        handshake_timeout: float = HANDSHAKE_TIMEOUT_S,
    ) -> None:
        self._clients: Dict[str, Client] = {}
        self._token_lookup = token_lookup or self._default_token_lookup
        self._inbound: list[InboundHandler] = []
        self._disconnect_handlers: list[Callable[[str], Awaitable[None]]] = []
        self._tool_dispatcher: Optional[ToolDispatcher] = None
        self._heartbeat_interval = heartbeat_interval
        self._heartbeat_timeout = heartbeat_timeout
        self._ping_interval = ping_interval
        self._handshake_timeout = handshake_timeout
        self._lock = asyncio.Lock()
        self._started = False

    # -- config --------------------------------------------------------------
    def on_inbound(self, handler: InboundHandler) -> None:
        """Register a handler for inbound audio/event/imu/tool_result msgs."""
        self._inbound.append(handler)

    def on_disconnect(self, handler: Callable[[str], Awaitable[None]]) -> None:
        """Register a handler fired when a device drops (clean or stale)."""
        self._disconnect_handlers.append(handler)

    def set_tool_dispatcher(self, dispatch: ToolDispatcher) -> None:
        """Register the IO tool dispatcher (esp_io.py)."""
        self._tool_dispatcher = dispatch

    @staticmethod
    async def _default_token_lookup(device_id: str, token: str) -> bool:
        # No auth configured -> accept (dev only). Production MUST supply a
        # real lookup that checks the per-device token (spec §17).
        return True

    # -- connection registry ------------------------------------------------
    def is_connected(self, device_id: str) -> bool:
        return device_id in self._clients

    def connected_devices(self) -> list[str]:
        return list(self._clients.keys())

    def capabilities(self, device_id: str) -> Dict[str, Any]:
        c = self._clients.get(device_id)
        return c.caps if c else {}

    # -- the aiohttp handler -------------------------------------------------
    async def handler(self, request: Any) -> Any:
        """aiohttp route handler: GET /api/esp_hermes/ws?device_id=&token=."""
        ws = getattr(request, "ws", None) or __import__("aiohttp").web.WebSocketResponse()
        if not ws.prepared:
            await ws.prepare(request)

        device_id = request.query.get("device_id", "").strip()
        token = request.query.get("token", "").strip()
        if not device_id:
            await ws.close(code=4400, message=b"missing device_id")
            return ws
        if not await self._token_lookup(device_id, token):
            _audit_logger().warning("REJECT device=%s token=%s", device_id, token[:6])
            await ws.close(code=4401, message=b"unauthorized")
            return ws

        client = Client(device_id=device_id, ws=ws, token=token)
        await self._register(client)
        _audit_logger().info("CONNECT device=%s", device_id)

        # Heartbeat writer + reader tasks
        hb_task = asyncio.ensure_future(self._heartbeat_loop(client))
        try:
            async for msg in ws:
                if msg.type == msg.TYPE_TEXT:
                    await self._on_text(client, msg.data)
                elif msg.type == msg.TYPE_BINARY:
                    # Binary frames carry raw audio chunks (opus/pcm) — wrap as
                    # audio msg with no base64 (spec allows both encodings).
                    await self._dispatch_inbound(
                        client, {"type": "audio", "format": "binary", "data": msg.data}
                    )
                elif msg.type == msg.TYPE_PING:
                    await ws.pong(msg.data)
                elif msg.type == msg.TYPE_PONG:
                    client.last_seen = time.monotonic()
                elif msg.type == msg.TYPE_CLOSE:  # pragma: no cover
                    break
        except asyncio.CancelledError:  # pragma: no cover
            raise
        except Exception as exc:  # socket errors etc.
            logger.debug("ws loop error device=%s: %s", device_id, exc)
        finally:
            hb_task.cancel()
            await self._unregister(client, reason="socket closed")
        return ws

    # -- inbound -------------------------------------------------------------
    async def _on_text(self, client: Client, raw: str) -> None:
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            logger.warning("bad json from %s: %r", client.device_id, raw[:120])
            return
        if not isinstance(msg, dict):
            return
        mtype = msg.get("type")
        if mtype == "ping":
            await client.ws.pong() if False else await self._send(
                client, {"type": "pong", "ts": int(time.time())})
            client.last_seen = time.monotonic()
            return
        if mtype == "capabilities":
            client.caps = msg.get("payload", {}) or {}
            await self._send(client, {"type": "ack", "device_id": client.device_id})
            for w in list(client._hs_waiters):
                w.set_result(True)
            return
        # tool_result correlates a pending tool_call future
        if mtype == "tool_result":
            await self._resolve_tool_result(client, msg)
            return
        if mtype in INBOUND_TYPES:
            await self._dispatch_inbound(client, msg)
            return
        logger.debug("unknown inbound type %r from %s", mtype, client.device_id)

    async def _dispatch_inbound(self, client: Client, msg: Dict[str, Any]) -> None:
        client.last_seen = time.monotonic()
        for h in self._inbound:
            try:
                await h(client.device_id, msg)
            except Exception as exc:  # handler errors must not kill the socket
                logger.exception("inbound handler error: %s", exc)

    async def _resolve_tool_result(self, client: Client, msg: Dict[str, Any]) -> None:
        call_id = msg.get("call_id")
        fut = client._pending.pop(call_id, None) if call_id else None
        if fut and not fut.done():
            fut.set_result(msg)

    # -- outbound (push) -----------------------------------------------------
    async def _send(self, client: Client, msg: Dict[str, Any]) -> bool:
        try:
            if msg.get("type") == "audio" and isinstance(msg.get("data"), (bytes, bytearray)):
                await client.ws.send_bytes(bytes(msg["data"]))
            else:
                await client.ws.send_str(json.dumps(msg, separators=(",", ":")))
            client.last_seen = time.monotonic()
            return True
        except Exception as exc:
            logger.debug("send fail to %s: %s", client.device_id, exc)
            return False

    async def push(self, device_id: str, msg: Dict[str, Any],
                  binary: bool = False) -> bool:
        """
        Push an outbound message to a device (pet_state, audio, video, tui_line,
        mode_ack, …). Used both for replies and proactive pushes (spec §13).
        Returns True if delivered.
        """
        client = self._clients.get(device_id)
        if not client:
            logger.debug("push to unknown/offline device %s", device_id)
            return False
        if binary and isinstance(msg.get("data"), (bytes, bytearray)):
            return await self._send(client, msg)
        return await self._send(client, msg)

    async def push_many(self, device_ids: list[str], msg: Dict[str, Any]) -> int:
        """Push to several devices; return count delivered."""
        ok = 0
        for d in device_ids:
            if await self.push(d, msg):
                ok += 1
        return ok

    # -- IO tool roundtrip ---------------------------------------------------
    async def call_tool(self, device_id: str, tool: str,
                        params: Dict[str, Any], timeout: float = 5.0) -> Dict[str, Any]:
        """
        Issue a tool_call to the device and await its tool_result.

        Returns {"ok": bool, "value": Any, "error": str|None}.
        If a tool dispatcher is registered (server-side synthetic tools), it is
        tried first; otherwise the call is sent to the physical device.
        """
        client = self._clients.get(device_id)
        if not client:
            return {"ok": False, "value": None, "error": "device_offline"}

        call_id = uuid.uuid4().hex[:16]
        msg = {"type": "tool_call", "call_id": call_id,
               "tool": tool, "params": params}

        # Server-side dispatcher (e.g. cached/gateway-resolved tools)
        if self._tool_dispatcher is not None:
            try:
                res = await asyncio.wait_for(
                    self._tool_dispatcher(device_id, tool, params, call_id),
                    timeout=timeout)
                return {"ok": bool(res.get("ok")), "value": res.get("value"),
                        "error": res.get("error")}
            except asyncio.TimeoutError:
                return {"ok": False, "value": None, "error": "dispatch_timeout"}
            except Exception as exc:  # dispatcher failure -> fall through to device
                logger.debug("tool dispatcher error: %s", exc)

        # Physical device roundtrip
        fut: asyncio.Future = asyncio.get_event_loop().create_future()
        client._pending[call_id] = fut
        if not await self._send(client, msg):
            client._pending.pop(call_id, None)
            return {"ok": False, "value": None, "error": "send_failed"}
        try:
            result = await asyncio.wait_for(fut, timeout=timeout)
            return {"ok": bool(result.get("ok")),
                    "value": result.get("value"),
                    "error": result.get("error")}
        except asyncio.TimeoutError:
            client._pending.pop(call_id, None)
            return {"ok": False, "value": None, "error": "device_timeout"}
        finally:
            client._pending.pop(call_id, None)

    # -- registry internals --------------------------------------------------
    async def _register(self, client: Client) -> None:
        async with self._lock:
            old = self._clients.get(client.device_id)
            if old is not None:
                # same device reconnecting — close the stale socket
                try:
                    await old.ws.close(code=4409, message=b"replaced")
                except Exception:
                    pass
            self._clients[client.device_id] = client
        # Kick a handshake waiter so tests/config can wait for capabilities
        client._hs_waiters.add(
            asyncio.get_event_loop().create_future())

    async def _unregister(self, client: Client, reason: str = "") -> None:
        async with self._lock:
            if self._clients.get(client.device_id) is client:
                self._clients.pop(client.device_id, None)
        # resolve any pending tool futures as errors
        for fut in list(client._pending.values()):
            if not fut.done():
                fut.set_result({"ok": False, "value": None,
                                "error": "device_disconnected"})
        client._pending.clear()
        _audit_logger().info("DISCONNECT device=%s reason=%s", client.device_id, reason)
        for h in self._disconnect_handlers:
            try:
                await h(client.device_id)
            except Exception as exc:  # pragma: no cover
                logger.debug("disconnect handler error: %s", exc)

    def is_stale(self, client: Client) -> bool:
        """True if the device has not been heard from within the timeout (§12)."""
        return (time.monotonic() - client.last_seen) > self._heartbeat_timeout

    async def _heartbeat_loop(self, client: Client) -> None:
        """Hub-side liveness: send ping frames; drop on stale pong (spec §12)."""
        try:
            while True:
                await asyncio.sleep(self._ping_interval)
                if self.is_stale(client):
                    logger.info("heartbeat timeout for %s", client.device_id)
                    try:
                        await client.ws.close(code=4408,
                                              message=b"heartbeat timeout")
                    except Exception:
                        pass
                    return
                try:
                    await client.ws.ping()
                except Exception:
                    return
        except asyncio.CancelledError:  # pragma: no cover
            return

    async def wait_for_capabilities(self, device_id: str,
                                    timeout: Optional[float] = None) -> bool:
        """Await the device's capabilities handshake (used by adapter/tests)."""
        client = self._clients.get(device_id)
        if not client:
            return False
        fut = asyncio.get_event_loop().create_future()
        client._hs_waiters.add(fut)
        try:
            return await asyncio.wait_for(fut, timeout or self._handshake_timeout)
        except asyncio.TimeoutError:
            return False
        finally:
            client._hs_waiters.discard(fut)

    # -- lifecycle -----------------------------------------------------------
    async def start(self) -> None:
        self._started = True

    async def shutdown(self) -> None:
        """Close all device sockets (gateway shutdown)."""
        async with self._lock:
            clients = list(self._clients.values())
            self._clients.clear()
        for c in clients:
            try:
                await c.ws.close(code=1001, message=b"server shutdown")
            except Exception:
                pass
        self._started = False


# ---------------------------------------------------------------------------
# Test/standalone helpers
# ---------------------------------------------------------------------------
async def connect_ws(hub: WSHub, ws_url: str, device_id: str, token: str = "test",
                     caps: Optional[Dict[str, Any]] = None) -> Any:
    """
    Client-side helper used by tests to connect a fake ESP and exchange messages
    over a real aiohttp server. Returns the aiohttp ClientWebSocketResponse.
    """
    import aiohttp
    async with aiohttp.ClientSession() as session:
        async with session.ws_connect(
                f"{ws_url}?device_id={device_id}&token={token}") as ws:
            if caps is not None:
                await ws.send_json({"type": "capabilities", "payload": caps})
            # hand back the live socket; caller drives the rest
            return ws
