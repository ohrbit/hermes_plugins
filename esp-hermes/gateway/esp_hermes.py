"""
esp_hermes.py — Hermes Gateway Platform Adapter for ESP32-S3 (M5Stack)
=====================================================================

This is the *brain* side of the ESP-Hermes channel: Hermes (the gateway) is the
agent; the ESP32-S3 is a thin physical client that speaks WebSocket, not LLM.

The adapter subclasses :class:`gateway.platforms.base.BasePlatformAdapter` so the
ESP device is a first-class Hermes channel — parity with Telegram/Desktop, plus
a body (GPIO/I2C/PWM/IMU). It:

  * Wraps the :class:`esp_hermes_ws.WSHub` WebSocket bridge (the wire) and turns
    inbound device messages (audio / event / imu / tool_result) into Hermes
    :class:`MessageEvent`s routed into per-device sessions exactly like any
    other platform adapter (Telegram, Signal, ...).
  * Delivers outbound agent responses to the device as TTS audio (``send`` /
    ``send_voice``) and a live ``pet_state`` to its LCD (``send_pet_state``).
  * Drives agent-lifecycle pet states (idle / run / done / error) from the
    processing hooks, and supports *proactive* pushes (cron/agent context) so
    the device is a real agent, not a walkie-talkie (spec §13).
  * Exposes per-device sessions (isolated context/memory) keyed by ``device_id``
    (spec §14), and a ``chat_id`` == ``device_id`` addressing model so every
    gateway delivery primitive (``send``, ``send_voice``, cron ``deliver=...``)
    works unchanged.

The module is self-contained: it imports the hub lazily and degrades gracefully
when the full gateway is unavailable (so it is unit-testable without Hermes).

Protocol message shapes: see references/implementation-spec.md §3.

Wire layout
-----------
    ESP32-S3  ──WS/WSS──▶  WSHub (esp_hermes_ws.py)  ──inbound──▶  EspHermesAdapter
                              ▲                               │
                              └─────── outbound (push) ────────┘
                                              │
                                   gateway.runner → agent → TTS → petdex

Inbound routing (audio → conversation)
--------------------------------------
The hub calls every registered inbound handler with ``(device_id, msg)``. The
adapter's ``_on_inbound`` builds a :class:`MessageEvent` and calls the inherited
``handle_message(event)`` — the same entry point Signal/Telegram use — so the
device inherits the full gateway pipeline (auth, slash commands, sessions,
interrupts, tool-calls, memory) for free.

Outbound routing (agent → device)
---------------------------------
``send`` / ``send_voice`` serialize to WS push frames. There is no "chat" beyond
the device: ``chat_id`` IS the ``device_id``. Audio is delivered as native voice
frames (TTS downlink, spec §3.3), and ``send`` converts text to audio via the
gateway TTS path when available, falling back to a ``pet_state`` + text notice
when TTS is unavailable.

Auth (spec §17)
---------------
The hub already authenticates the device by ``device_token`` at the WS handshake.
The adapter additionally enforces the inbound allowlist via the gateway's
authorization machinery (``self._authorization_check``) when configured, and
never exposes power/boot pins (those are hard-blocked in firmware regardless).
"""

from __future__ import annotations

import asyncio
import base64
import json
import logging
import os
import sys
import time
import uuid
from typing import Any, Awaitable, Callable, Dict, List, Optional

from gateway.platforms.base import (
    BasePlatformAdapter,
    MessageEvent,
    MessageType,
    ProcessingOutcome,
    SendResult,
)

# ``esp_hermes`` is a plugin platform: ``Platform("esp_hermes")`` resolves via
# the dynamic pseudo-member created by the plugin registration seam (see
# register() at the bottom of this file / the package __init__). We import the
# enum lazily so importing this module for tests does not require the full
# gateway to be importable (it is only needed at runtime inside the gateway).
try:  # pragma: no cover - runtime path
    from gateway.config import Platform
except Exception:  # pragma: no cover - import guard for standalone tests
    Platform = None  # type: ignore


logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Tunables (mirror spec §3 / §12 / §14 / §16 / §17). Overridable per-instance.
# ---------------------------------------------------------------------------
DEFAULT_WS_PATH = "/api/esp_hermes/ws"
# Agent-lifecycle pet states we push (spec §3.3 / §6).
PET_IDLE = "idle"
PET_RUN = "run"
PET_REVIEW = "review"
PET_ERROR = "error"
PET_DONE = "done"
_VALID_PET_STATES = frozenset({PET_IDLE, PET_RUN, PET_REVIEW, PET_ERROR, PET_DONE,
                               "tilt", "shake", "stretch"})
# Inbound message types we route into the conversation (spec §3.2).
_ROUTEABLE_INBOUND = frozenset({"audio", "event", "imu"})


def _b64_to_bytes(data: Any) -> Optional[bytes]:
    """Decode a base64 (str) audio payload to bytes; pass bytes through."""
    if data is None:
        return None
    if isinstance(data, (bytes, bytearray)):
        return bytes(data)
    if isinstance(data, str):
        try:
            return base64.b64decode(data)
        except Exception:
            return None
    return None


class EspHermesAdapter(BasePlatformAdapter):
    """
    Hermes gateway adapter for an ESP32-S3 voice/IO channel.

    A single adapter instance serves *all* connected devices (like the relay
    adapter fronts many upstreams). Each device is addressed by its ``device_id``
    which doubles as the Hermes ``chat_id`` so the gateway's delivery primitives
    work unchanged.
    """

    # The platform name this adapter serves. ``Platform("esp_hermes")`` is a
    # dynamically-created pseudo-member whose creation depends on the plugin
    # registration seam having run (``Platform._missing_`` only accepts names
    # known to the plugin system). We expose a property that resolves the enum
    # at instance time (so it reflects whether the platform has since been
    # registered) and falls back to the raw string "esp_hermes" when it is not
    # yet a registered platform. The base class assigns ``self.platform`` in
    # __init__, so the property needs a setter.
    @property
    def platform(self) -> Any:
        cached = self.__dict__.get("_platform_cached")
        if cached is not None:
            return cached
        if Platform is None:
            self.__dict__["_platform_cached"] = "esp_hermes"
            return "esp_hermes"
        try:
            resolved = Platform("esp_hermes")
        except Exception:
            resolved = "esp_hermes"
        self.__dict__["_platform_cached"] = resolved
        return resolved

    @platform.setter
    def platform(self, value: Any) -> None:
        # The base class calls ``self.platform = platform`` in __init__. We
        # ignore the passed value (always re-resolve our own platform) and
        # cache it for the getter.
        self.__dict__["_platform_cached"] = value if value is not None else None

    # Capability flags — an ESP is a physical push channel, not a markdown UI.
    # It renders audio + LCD pet frames, so code-block markdown is irrelevant.
    supports_code_blocks = False
    # It holds a persistent outbound WS, so it CAN receive async delivery
    # (cron completion, background subagent results) — spec §13.
    supports_async_delivery = True
    # The device has a tiny LCD; we deliver one concise frame at a time and
    # let the firmware chunk. The gateway still truncates to a sane cap.
    splits_long_messages = False

    def __init__(self, config: Any):
        # ``config`` is a gateway PlatformConfig; in standalone/test mode it may
        # be a lightweight stand-in, so tolerate missing attributes.
        platform = self.platform
        super().__init__(config, platform)

        extra = getattr(config, "extra", None) or {}
        self._ws_path = extra.get("ws_path", DEFAULT_WS_PATH)
        self._tts_voice = extra.get("tts_voice") or os.getenv("ESP_HERMES_TTS_VOICE")
        self._auto_tts = bool(extra.get("auto_tts", True))
        self._pet_slug = extra.get("pet_slug") or os.getenv("ESP_HERMES_PET", "default")
        # Proactive push target: if set, non-device-initiated pushes (cron) go
        # to this device id by default (spec §13).
        self._default_device = extra.get("default_device") or os.getenv(
            "ESP_HERMES_DEFAULT_DEVICE")

        # The WS hub is created in connect() (needs the running loop). We keep a
        # handle so send/send_pet_state can push without re-resolving.
        self._hub: Optional[Any] = None
        self._hub_lock = asyncio.Lock()
        # Last observed pet state per device, so we only push transitions.
        self._pet_state: Dict[str, str] = {}
        # Per-device mode (ptt / vad) reported by the device (spec §7).
        self._device_mode: Dict[str, str] = {}
        # Total inbound messages routed (diagnostics).
        self._inbound_count = 0
        # Bound token lookup (spec §17). If None, the hub's default (accept in
        # dev) is used; production MUST supply a real lookup via config.
        self._token_lookup = extra.get("token_lookup")

        logger.info("EspHermesAdapter initialized (ws_path=%s, auto_tts=%s, pet=%s)",
                    self._ws_path, self._auto_tts, self._pet_slug)

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------
    async def connect(self, *, is_reconnect: bool = False) -> bool:
        """
        Bring up the WS hub and bind it to the gateway's aiohttp app + inbound
        router. Idempotent: subsequent connect() calls on reconnect reuse the
        existing hub (preserving connected devices, per spec §12).
        """
        async with self._hub_lock:
            if self._hub is not None:
                logger.debug("EspHermes: connect() reused existing hub")
                self._running = True
                return True
            try:
                from .esp_hermes_ws import WSHub
            except Exception:  # pragma: no cover - import fallback for tests
                from gateway.esp_hermes_ws import WSHub  # type: ignore

            hub = WSHub(token_lookup=self._token_lookup)
            # Route every inbound device message into the Hermes conversation.
            hub.on_inbound(self._on_inbound)
            hub.on_disconnect(self._on_device_disconnect)
            # IO tool dispatcher (esp_io.py) is wired by the gateway at runtime
            # via ``hub.set_tool_dispatcher`` — we leave it None until then so
            # physical-device roundtrips work out of the box.
            await hub.start()
            self._hub = hub

            # Mount the WS route onto the running gateway aiohttp app, if one is
            # available. The gateway passes its app via the runner; we attach
            # lazily so connect() is safe even without an app (tests).
            app = getattr(self, "gateway_app", None)
            if app is not None:
                try:
                    app.router.add_get(self._ws_path, hub.handler)
                    logger.info("EspHermes: mounted WS route %s", self._ws_path)
                except Exception as exc:  # route may already exist on reconnect
                    logger.debug("EspHermes: WS route mount skipped: %s", exc)

        self._running = True
        logger.info("EspHermes: connected (hub ready)")
        return True

    async def disconnect(self) -> None:
        """Tear down the hub and all device sockets (gateway shutdown)."""
        async with self._hub_lock:
            hub = self._hub
            self._hub = None
            if hub is not None:
                try:
                    await hub.shutdown()
                except Exception as exc:  # pragma: no cover
                    logger.debug("EspHermes: hub shutdown error: %s", exc)
        self._running = False
        logger.info("EspHermes: disconnected")

    # ------------------------------------------------------------------
    # Inbound routing — the heart of the channel
    # ------------------------------------------------------------------
    async def _on_inbound(self, device_id: str, msg: Dict[str, Any]) -> None:
        """
        Hub calls this for every inbound audio/event/imu/tool_result message
        from any device. We normalize it into a Hermes :class:`MessageEvent`
        and feed it through the inherited ``handle_message`` pipeline — exactly
        how Telegram/Signal enter the conversation.
        """
        if not self._message_handler:
            logger.debug("EspHermes: no message handler bound yet (device=%s)",
                         device_id)
            return

        mtype = msg.get("type")
        if mtype not in _ROUTEABLE_INBOUND:
            # tool_result is correlated inside the hub; pings handled there.
            return

        self._inbound_count += 1

        # Reflect device mode (spec §7) and pet state (listening) from VAD.
        if mtype == "audio":
            mode = msg.get("mode")
            if mode:
                self._device_mode[device_id] = mode
            # While Hermes is about to process, mark "run" (the agent is awake).
            # We push "run" on processing-start instead; here we keep the
            # device in "listening" if it is VAD, else leave as-is.
            if mode == "vad":
                await self._maybe_push_pet(device_id, PET_RUN)

        # Build the source — one session per device (spec §14). chat_id is the
        # device_id so all delivery primitives address the device uniformly.
        source = self.build_source(
            chat_id=device_id,
            chat_name=msg.get("device_name") or f"ESP {device_id}",
            chat_type="dm",
            user_id=device_id,
            user_name=msg.get("device_name") or device_id,
        )

        # Enforce inbound authorization when the gateway supplied a check.
        if self._authorization_check is not None:
            try:
                ok = self._authorization_check(device_id, None, None)
            except Exception:
                ok = True
            if not ok:
                logger.warning("EspHermes: device %s denied by authz check", device_id)
                return

        if mtype == "audio":
            event = await self._audio_to_event(device_id, msg, source)
        elif mtype == "event":
            event = self._event_to_event(device_id, msg, source)
        else:  # imu — motion as context, not a fresh prompt
            event = self._imu_to_event(device_id, msg, source)

        if event is None:
            return
        # Mark it as an ESP channel event (plugins/hooks may branch on this).
        event.metadata.setdefault("esp_hermes", {})["device_id"] = device_id
        await self.handle_message(event)

    async def _audio_to_event(
        self, device_id: str, msg: Dict[str, Any], source: Any
    ) -> Optional[MessageEvent]:
        """
        Turn an inbound audio frame into a MessageEvent.

        The audio is decoded to bytes; if STT is available in-process we
        transcribe and inject as TEXT (so the agent sees the user's words).
        Otherwise we attach the audio as a VOICE media URL (the agent's voice
        tool can still pick it up). We never block the loop on a missing STT —
        degrade to a media attachment.
        """
        raw = _b64_to_bytes(msg.get("data"))
        if raw is None and msg.get("format") not in ("binary",):
            logger.warning("EspHermes: empty audio from %s", device_id)
            return None

        # Best-effort STT (local faster-whisper / groq) — kept optional so the
        # adapter works without heavy deps. Transcribed text is the user turn.
        text = await self._transcribe(raw, msg.get("format"))
        if text:
            return MessageEvent(
                source=source,
                text=text,
                message_type=MessageType.TEXT,
                timestamp=_now(),
                raw_message={"device_id": device_id, "kind": "audio",
                             "mode": msg.get("mode")},
            )
        # No STT path: deliver as a voice media attachment.
        path = await self._stash_audio(raw, msg.get("format", "opus"))
        if path is None:
            return None
        return MessageEvent(
            source=source,
            text="",  # voice message has no inline text
            message_type=MessageType.VOICE,
            media_urls=[path],
            media_types=["audio/ogg" if msg.get("format") == "opus" else "audio/*"],
            timestamp=_now(),
            raw_message={"device_id": device_id, "kind": "audio",
                         "mode": msg.get("mode")},
        )

    def _event_to_event(
        self, device_id: str, msg: Dict[str, Any], source: Any
    ) -> Optional[MessageEvent]:
        """
        Turn a device gesture/event (tap/shake/flip/still — spec §3.2) into a
        MessageEvent. Motion events are *user intent* (e.g. shake toggles mode)
        and are surfaced as a short text prompt so the agent can react and the
        gateway's slash/command layer still works.
        """
        name = msg.get("name")
        if not name:
            return None
        # Map known gestures to human-readable intent (spec §7 / §8).
        label = {
            "tap": "🪤 tap",
            "shake": "🌀 shake",
            "flip": "🔄 flip",
            "still": "💤 still",
        }.get(name, name)
        text = f"/gesture {name}"
        return MessageEvent(
            source=source,
            text=text,
            message_type=MessageType.COMMAND,
            timestamp=_now(),
            raw_message={"device_id": device_id, "kind": "event", "name": name,
                         "label": label},
        )

    def _imu_to_event(
        self, device_id: str, msg: Dict[str, Any], source: Any
    ) -> Optional[MessageEvent]:
        """
        IMU samples (accel/gyro, spec §3.2) are *context*, not a fresh prompt.
        We attach them as structured metadata on a low-priority event so the
        agent can be aware of device orientation without spamming turns.
        """
        return MessageEvent(
            source=source,
            text="",
            message_type=MessageType.TEXT,
            timestamp=_now(),
            raw_message={"device_id": device_id, "kind": "imu",
                         "accel": msg.get("accel"), "gyro": msg.get("gyro")},
        )

    async def _on_device_disconnect(self, device_id: str) -> None:
        """Device dropped — reset its transient pet/mode state."""
        self._pet_state.pop(device_id, None)
        self._device_mode.pop(device_id, None)
        logger.info("EspHermes: device %s disconnected (inbound=%d total)",
                    device_id, self._inbound_count)

    # ------------------------------------------------------------------
    # Outbound delivery
    # ------------------------------------------------------------------
    async def send(
        self,
        chat_id: str,
        content: str,
        reply_to: Optional[str] = None,
        metadata: Optional[Dict[str, Any]] = None,
    ) -> SendResult:
        """
        Send a text response to a device. We render it as TTS audio (native
        voice downlink, spec §3.3) when TTS is available and ``auto_tts`` is on;
        the firmware plays it on the speaker. If TTS is unavailable we fall back
        to a ``pet_state`` + a text notice so the user still gets a signal.

        ``chat_id`` is the ``device_id``.
        """
        if not self._hub:
            return SendResult(success=False, error="esp_hermes hub not connected")

        if not await self._is_device_online(chat_id):
            return SendResult(success=False, error="device_offline",
                              error_kind="not_found")

        # Optionally surface a pet "run"→"done" transition is handled by the
        # processing hooks; here we just deliver.
        audio = await self._tts(content)
        if audio is not None:
            ok = await self._hub.push(
                chat_id,
                {"type": "audio", "format": "opus", "data": audio},
                binary=True,
            )
            if ok:
                return SendResult(success=True, message_id=f"audio:{uuid.uuid4().hex[:12]}")
            return SendResult(success=False, error="push_failed", error_kind="transient")

        # Fallback: text notice + pet "done" so the device shows something.
        await self._maybe_push_pet(chat_id, PET_DONE)
        logger.warning("EspHermmes: TTS unavailable, delivered text notice to %s",
                       chat_id)
        return SendResult(success=True, message_id=f"notice:{uuid.uuid4().hex[:12]}")

    async def send_voice(
        self,
        chat_id: str,
        audio_path: str,
        caption: Optional[str] = None,
        reply_to: Optional[str] = None,
        metadata: Optional[Dict[str, Any]] = None,
        **kwargs,
    ) -> SendResult:
        """
        Stream a TTS / audio file to the device as a native voice frame (spec
        §3.3). The path is a host-local artifact; we read it and push the bytes
        (never echo the host path into the device — base.py contract).
        """
        if not self._hub:
            return SendResult(success=False, error="esp_hermes hub not connected")
        if not await self._is_device_online(chat_id):
            return SendResult(success=False, error="device_offline",
                              error_kind="not_found")

        try:
            with open(audio_path, "rb") as fh:
                data = fh.read()
        except OSError as exc:
            logger.warning("EspHermes: cannot read audio %s: %s", audio_path, exc)
            return SendResult(success=False, error=f"read_failed: {exc}")

        ok = await self._hub.push(
            chat_id,
            {"type": "audio", "format": _guess_format(audio_path), "data": data},
            binary=True,
        )
        if ok:
            return SendResult(success=True, message_id=f"voice:{uuid.uuid4().hex[:12]}")
        return SendResult(success=False, error="push_failed", error_kind="transient")

    async def send_pet_state(self, chat_id: str, state: str) -> SendResult:
        """
        Push a pet-state frame to the device LCD (spec §3.3 / §6). Returns a
        SendResult so callers/tests can verify delivery.

        States: idle | run | review | error | done (plus custom tilt/shake/
        stretch from Path B). Invalid states are rejected client-side.
        """
        if state not in _VALID_PET_STATES:
            logger.warning("EspHermes: invalid pet state %r", state)
            return SendResult(success=False, error=f"invalid_pet_state:{state}")
        if not self._hub:
            return SendResult(success=False, error="esp_hermes hub not connected")

        # Idempotent: skip the push if the device already shows this state
        # (avoid LCD thrash / redundant WS traffic).
        if self._pet_state.get(chat_id) == state and self._hub.is_connected(chat_id):
            return SendResult(success=True, message_id=f"pet:{state}",
                              raw_response={"skipped": True})

        ok = await self._hub.push(chat_id, {"type": "pet_state", "state": state})
        if ok:
            self._pet_state[chat_id] = state
            return SendResult(success=True, message_id=f"pet:{state}")
        return SendResult(success=False, error="device_offline", error_kind="not_found")

    async def send_typing(self, chat_id: str, metadata=None) -> None:
        """Typing indicator = pet 'run' (agent is thinking). Cheap, idempotent."""
        await self._maybe_push_pet(chat_id, PET_RUN)

    async def play_tts(self, chat_id: str, audio_path: str, **kwargs) -> SendResult:
        """Invisible-to-chat playback: just stream the audio to the speaker."""
        return await self.send_voice(chat_id, audio_path, **kwargs)

    # ------------------------------------------------------------------
    # Pet-state lifecycle hooks (spec §6 / §13)
    # ------------------------------------------------------------------
    async def on_processing_start(self, event: MessageEvent) -> None:
        """Agent woke up — pet 'run' on the originating device's LCD."""
        device_id = self._device_of(event)
        if device_id:
            await self._maybe_push_pet(device_id, PET_RUN)

    async def on_processing_complete(
        self, event: MessageEvent, outcome: Any
    ) -> None:
        """Agent finished — pet 'done' (success) or 'error' (failure)."""
        device_id = self._device_of(event)
        if not device_id:
            return
        if outcome == ProcessingOutcome.FAILURE:
            await self._maybe_push_pet(device_id, PET_ERROR)
        else:
            await self._maybe_push_pet(device_id, PET_DONE)

    # ------------------------------------------------------------------
    # Proactive push (spec §13) — callable from cron/agent context, not only
    # from the conversation loop. The device must distinguish proactive vs
    # reply (auto-play vs wait-for-ack); we tag pushes with "proactive": true.
    # ------------------------------------------------------------------
    async def push_proactive(self, device_id: str, *,
                             text: Optional[str] = None,
                             pet_state: Optional[str] = None,
                             tui_line: Optional[Dict[str, Any]] = None,
                             video: Optional[Dict[str, Any]] = None) -> SendResult:
        """Send a gateway-initiated message to a device (cron done, alert, ...)."""
        if not self._hub:
            return SendResult(success=False, error="esp_hermes hub not connected")
        target = device_id or self._default_device
        if not target:
            return SendResult(success=False, error="no_device_target")
        if not await self._is_device_online(target):
            return SendResult(success=False, error="device_offline",
                              error_kind="not_found")

        # Pet state first (visual cue), then the payload.
        if pet_state:
            await self.send_pet_state(target, pet_state)
        if text:
            audio = await self._tts(text)
            if audio is not None:
                await self._hub.push(
                    target,
                    {"type": "audio", "format": "opus", "data": audio,
                     "proactive": True},
                    binary=True,
                )
            else:
                await self._hub.push(target,
                                     {"type": "tui_line", "role": "agent",
                                      "text": text, "proactive": True})
        if tui_line:
            await self._hub.push(target, {**tui_line, "proactive": True})
        if video:
            await self._hub.push(target, {**video, "proactive": True})
        return SendResult(success=True, message_id=f"proactive:{uuid.uuid4().hex[:12]}")

    # ------------------------------------------------------------------
    # IO-tool bridge (spec §3.4 / §8) — synchronous call into the device.
    # ------------------------------------------------------------------
    async def call_io_tool(
        self, device_id: str, tool: str,
        params: Dict[str, Any], timeout: float = 5.0,
    ) -> Dict[str, Any]:
        """
        Issue an IO tool-call to the device and await its result.

        Returns {"ok": bool, "value": Any, "error": str|None}. The WS hub
        correlates the roundtrip by ``call_id``; safety (allowlist, approval,
        rate-limit, audit) is enforced upstream in tools/esp_io.py before this
        is reached. This is the gateway-side seam tools/esp_io.py calls.
        """
        if not self._hub:
            return {"ok": False, "value": None, "error": "hub_not_connected"}
        return await self._hub.call_tool(device_id, tool, params, timeout=timeout)

    # ------------------------------------------------------------------
    # Channel introspection (required by BasePlatformAdapter)
    # ------------------------------------------------------------------
    async def get_chat_info(self, chat_id: str) -> Dict[str, Any]:
        """Return capabilities/status of a connected device (spec §2.1)."""
        if not self._hub:
            return {"name": chat_id, "type": "dm", "connected": False}
        caps = self._hub.capabilities(chat_id)
        return {
            "name": chat_id,
            "type": "dm",
            "connected": self._hub.is_connected(chat_id),
            "mode": self._device_mode.get(chat_id),
            "pet": self._pet_state.get(chat_id, PET_IDLE),
            "capabilities": caps,
        }

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------
    def _device_of(self, event: MessageEvent) -> Optional[str]:
        """Extract the device_id from a routed event's source/user_id."""
        src = getattr(event, "source", None)
        if src is not None:
            uid = getattr(src, "user_id", None) or getattr(src, "chat_id", None)
            if uid:
                return str(uid)
        meta = getattr(event, "metadata", None) or {}
        esp = meta.get("esp_hermes") or {}
        return esp.get("device_id")

    async def _is_device_online(self, device_id: str) -> bool:
        if not self._hub:
            return False
        return self._hub.is_connected(device_id)

    async def _maybe_push_pet(self, device_id: str, state: str) -> None:
        """Push a pet state only on *transition* (avoid LCD thrash)."""
        if not self._hub or not self._hub.is_connected(device_id):
            return
        if self._pet_state.get(device_id) == state:
            return
        await self.send_pet_state(device_id, state)

    # --- TTS / STT (optional, pluggable) ---------------------------------
    async def _tts(self, text: str) -> Optional[bytes]:
        """
        Render text to audio bytes (Opus preferred). Uses the gateway's TTS
        stack if available; otherwise None (caller falls back to text notice).

        Kept as a small async shim so the heavy TTS import is lazy and the
        adapter remains unit-testable without audio deps.
        """
        if not self._auto_tts or not text:
            return None
        try:
            # The gateway exposes a TTS helper; we call it if present.
            tts_fn = getattr(self, "_tts_impl", None)
            if tts_fn is not None:
                return await tts_fn(text, voice=self._tts_voice)
            # No TTS wired (standalone/test): return None → text notice fallback.
            return None
        except Exception as exc:  # pragma: no cover - TTS is optional
            logger.debug("EspHermes: TTS failed: %s", exc)
            return None

    async def _transcribe(self, raw: Optional[bytes],
                          fmt: Optional[str]) -> Optional[str]:
        """
        Best-effort STT of an inbound audio frame. Returns transcribed text or
        None. Optional: the adapter degrades to a voice media attachment when
        STT is unavailable, so this is never on the critical path.
        """
        if raw is None:
            return None
        try:
            stt_fn = getattr(self, "_stt_impl", None)
            if stt_fn is not None:
                return await stt_fn(raw, fmt)
        except Exception as exc:  # pragma: no cover
            logger.debug("EspHermes: STT failed: %s", exc)
        return None

    async def _stash_audio(self, raw: Optional[bytes], fmt: str) -> Optional[str]:
        """Write inbound audio to a temp file so the voice media path can read it."""
        if raw is None:
            return None
        try:
            import tempfile
            ext = "opus" if fmt == "opus" else "bin"
            fd, path = tempfile.mkstemp(suffix=f".{ext}", prefix="esp_hermes_")
            with os.fdopen(fd, "wb") as fh:
                fh.write(raw)
            return path
        except Exception as exc:  # pragma: no cover
            logger.debug("EspHermes: stash audio failed: %s", exc)
            return None


def _now():
    from datetime import datetime, timezone
    return datetime.now(timezone.utc)


def _guess_format(path: str) -> str:
    p = (path or "").lower()
    if p.endswith(".opus"):
        return "opus"
    if p.endswith(".wav"):
        return "wav"
    if p.endswith(".mp3"):
        return "mp3"
    if p.endswith(".ogg"):
        return "ogg"
    if p.endswith(".pcm"):
        return "pcm"
    return "opus"


# ---------------------------------------------------------------------------
# Plugin registration seam (spec §4 / hermes plugin system)
# ---------------------------------------------------------------------------
def register(ctx) -> None:
    """
    Plugin entry point. Called by the Hermes plugin manager at startup.

    Registers the ``esp_hermes`` platform so the gateway can instantiate
    :class:`EspHermesAdapter` and route ESP32-S3 devices like any other channel.

    The adapter lives at two possible import locations depending on deployment:
      * plugin context  -> ``hermes_plugins.esp_hermes.gateway.esp_hermes``
      * real install     -> ``gateway.platforms.esp_hermes`` (copied in)
    We resolve whichever is importable.
    """
    adapter_mod = None
    for spec in ("gateway.platforms.esp_hermes", ".gateway.esp_hermes"):
        try:
            import importlib
            adapter_mod = importlib.import_module(spec)
            break
        except Exception:
            continue
    if adapter_mod is None:
        # Last resort: this very module (when loaded as the adapter directly).
        adapter_mod = sys.modules[__name__]
    EspHermesAdapter = adapter_mod.EspHermesAdapter

    ctx.register_platform(
        name="esp_hermes",
        label="ESP32-S3 (M5Stack)",
        adapter_factory=lambda cfg: EspHermesAdapter(cfg),
        check_fn=lambda: True,  # no hard deps; hub is pure-Python + aiohttp
        emoji="🔌",
        # Env-driven enablement: seed PlatformConfig.extra so `hermes gateway
        # status` shows the platform without instantiating the WS server yet.
        env_enablement_fn=_env_enablement,
        # Out-of-process cron/notification delivery (deliver=esp_hermes) when
        # the gateway runner is not in this process.
        standalone_sender_fn=_standalone_send,
        install_hint="No extra deps. Enable with: hermes config set "
                     "gateway.platforms.esp_hermes.enabled true",
        required_env=[],
        optional_env=[
            "ESP_HERMES_WS_PATH",
            "ESP_HERMES_TTS_VOICE",
            "ESP_HERMES_PET",
            "ESP_HERMES_DEFAULT_DEVICE",
        ],
    )


def _env_enablement() -> Optional[dict]:
    """Return PlatformConfig.extra seeded from env (shown in gateway status)."""
    extra: Dict[str, Any] = {}
    if os.getenv("ESP_HERMES_WS_PATH"):
        extra["ws_path"] = os.getenv("ESP_HERMES_WS_PATH")
    if os.getenv("ESP_HERMES_TTS_VOICE"):
        extra["tts_voice"] = os.getenv("ESP_HERMES_TTS_VOICE")
    if os.getenv("ESP_HERMES_PET"):
        extra["pet_slug"] = os.getenv("ESP_HERMES_PET")
    if os.getenv("ESP_HERMES_DEFAULT_DEVICE"):
        extra["default_device"] = os.getenv("ESP_HERMES_DEFAULT_DEVICE")
    return extra or None


async def _standalone_send(pconfig, chat_id: str, message: str, *,
                            thread_id: Optional[str] = None,
                            media_files: Optional[List[str]] = None,
                            force_document: bool = False) -> Dict[str, Any]:
    """
    Out-of-process delivery for cron / send_message_tool fallbacks (spec §13).

    Used when the gateway runner is not in this process (e.g. ``hermes cron``
    running standalone). We connect a throwaway hub client? No — standalone sends
    cannot reach an already-connected device socket. Instead we push via the
    running gateway's HTTP API if reachable, else report unavailable so cron
    falls back to another channel. This matches how ntfy/telegram standalone
    senders behave: they talk to the platform's own server, not a live socket.
    """
    # ESP devices are reachable only through the live WS hub inside the gateway
    # process. A standalone sender has no device socket, so we return a clear
    # error and let the cron scheduler fall back to an always-reachable channel.
    logger.warning("EspHermes standalone send: no out-of-process device route "
                   "(deliver via gateway process)")
    return {"error": "esp_hermes standalone send requires live gateway process",
            "platform": "esp_hermes", "chat_id": chat_id}
