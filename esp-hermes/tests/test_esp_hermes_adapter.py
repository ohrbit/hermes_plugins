"""
Self-contained test for the EspHermesAdapter gateway adapter.

Strategy
--------
The adapter is designed to live inside Hermes's ``gateway/platforms/`` package,
where ``from gateway.platforms.base import BasePlatformAdapter`` resolves to the
real base class. To test it without disturbing the install, we:

  1. Put Hermes's install dir first on sys.path so ``gateway`` => Hermes.
  2. Load our repo's ``esp_hermes_ws.py`` as ``gateway.platforms.esp_hermes_ws``
     and ``esp_hermes.py`` as ``gateway.platforms.esp_hermes`` via
     spec_from_file_location (no CWD shadowing).
  3. Pre-register the ``esp_hermes`` platform in the gateway's registry so
     ``Platform("esp_hermes")`` resolves through ``Platform._missing_``.
  4. Exercise real routing: inbound messages -> MessageEvent -> handle_message;
     outbound sends -> WSHub.push frames (recorded on a fake device socket);
     lifecycle hooks -> pet-state transitions; proactive push + IO tool roundtrip.

No network, no hardware, no audio deps required.
"""

import asyncio
import importlib.util
import os
import sys

# --- 1. Hermes install first on path --------------------------------------
HERMES_ROOT = "/usr/local/lib/hermes-agent"
if HERMES_ROOT not in sys.path:
    sys.path.insert(0, HERMES_ROOT)

REPO_GATEWAY = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "gateway")
)


def _load_module(name: str, path: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


# --- 2. Load WS hub + adapter as gateway.platforms.* ----------------------
ws_mod = _load_module(
    "gateway.platforms.esp_hermes_ws",
    os.path.join(REPO_GATEWAY, "esp_hermes_ws.py"),
)
adapter_mod = _load_module(
    "gateway.platforms.esp_hermes",
    os.path.join(REPO_GATEWAY, "esp_hermes.py"),
)

EspHermesAdapter = adapter_mod.EspHermesAdapter
WSHub = ws_mod.WSHub

# --- 3. Pre-register platform so Platform("esp_hermes") resolves ----------
from gateway.platform_registry import platform_registry
from gateway.platforms.base import Platform  # noqa: F401  (ensure enum importable)

platform_registry.register(
    type(
        "PlatformEntry",
        (),
        {
            "name": "esp_hermes",
            "label": "ESP32-S3 (M5Stack)",
            "adapter_factory": lambda cfg: EspHermesAdapter(cfg),
            "check_fn": lambda: True,
            "source": "plugin",
            "emoji": "🔌",
        },
    )()
)


# --- Fake plumbing ---------------------------------------------------------
class FakeWS:
    """Records sent frames for assertions."""

    def __init__(self):
        self.frames = []
        self.closed = False

    async def send_str(self, s):
        self.frames.append(("str", s))

    async def send_bytes(self, b):
        self.frames.append(("bytes", b))

    async def close(self, code=None, message=None):
        self.closed = True


class FakeClient:
    def __init__(self, device_id, ws):
        self.device_id = device_id
        self.ws = ws
        self.token = "tok"
        self.caps = {"imu": True, "pins": [12, 13]}
        self.last_seen = 0
        self.connected_at = 0
        self._pending = {}
        self._hs_waiters = set()


class FakeConfig:
    extra = {}


class FakeMessageHandler:
    """Stands in for the gateway runner's message handler."""

    def __init__(self):
        self.events = []

    async def __call__(self, event):
        self.events.append(event)


def _connected_adapter():
    """Build + connect an adapter with a fake device already in the hub.

    Returns a coroutine that, when awaited, yields (adapter, handler, ws).
    """
    async def _setup():
        adapter = EspHermesAdapter(FakeConfig())
        handler = FakeMessageHandler()
        adapter._message_handler = handler
        await adapter.connect()
        ws = FakeWS()
        client = FakeClient("dev42", ws)
        adapter._hub._clients["dev42"] = client
        return adapter, handler, ws
    return _setup()


# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
def test_platform_resolves():
    p = Platform("esp_hermes")
    assert str(p.value) == "esp_hermes"


def test_inbound_audio_routes_to_event():
    async def run():
        adapter, handler, _ = await _connected_adapter()
        await adapter._on_inbound(
            "dev42",
            {"type": "audio", "format": "opus",
             "data": "AAAA", "mode": "ptt"},
        )
        # handle_message spawns the routing as a background task; let it run.
        await asyncio.sleep(0.02)
        # Without STT wired, audio with no decode -> event still created as
        # VOICE media (stashed). With our fake base64 it decodes to bytes.
        assert len(handler.events) == 1
        ev = handler.events[0]
        assert ev.source.chat_id == "dev42"
        assert ev.source.user_id == "dev42"
        assert ev.metadata["esp_hermes"]["device_id"] == "dev42"
        await adapter.disconnect()
    asyncio.run(run())


def test_inbound_event_gesture_becomes_command():
    async def run():
        adapter, handler, _ = await _connected_adapter()
        await adapter._on_inbound("dev42", {"type": "event", "name": "shake"})
        await asyncio.sleep(0.02)
        assert len(handler.events) == 1
        ev = handler.events[0]
        assert ev.text == "/gesture shake"
        assert ev.message_type.value == "command"
        await adapter.disconnect()
    asyncio.run(run())


def test_inbound_mode_vad_pushes_run_pet():
    async def run():
        adapter, handler, ws = await _connected_adapter()
        await adapter._on_inbound(
            "dev42", {"type": "audio", "format": "opus", "data": "AAAA", "mode": "vad"}
        )
        # vad -> pet_state "run" pushed on inbound (listening->run)
        pet_frames = [f for f in ws.frames if "pet_state" in f[1]]
        assert any('"state":"run"' in f[1] for f in pet_frames)
        await adapter.disconnect()
    asyncio.run(run())


def test_send_pet_state():
    async def run():
        adapter, _, ws = await _connected_adapter()
        res = await adapter.send_pet_state("dev42", "done")
        assert res.success is True
        assert any('"state":"done"' in f[1] for f in ws.frames
                   if f[0] == "str")
        # repeated same state -> no duplicate push (idempotent)
        ws2_count = len([f for f in ws.frames if '"state":"done"' in f[1]])
        await adapter.send_pet_state("dev42", "done")
        ws3_count = len([f for f in ws.frames if '"state":"done"' in f[1]])
        assert ws3_count == ws2_count
        await adapter.disconnect()
    asyncio.run(run())


def test_send_pet_state_invalid_rejected():
    async def run():
        adapter, _, _ = await _connected_adapter()
        res = await adapter.send_pet_state("dev42", "explode")
        assert res.success is False
        await adapter.disconnect()
    asyncio.run(run())


def test_send_text_falls_back_to_notice_no_tts():
    async def run():
        adapter, _, ws = await _connected_adapter()
        res = await adapter.send("dev42", "Hello from Hermes")
        # No TTS wired -> fallback notice + pet "done"
        assert res.success is True
        assert any('"state":"done"' in f[1] for f in ws.frames
                   if f[0] == "str")
        await adapter.disconnect()
    asyncio.run(run())


def test_send_voice_streams_bytes():
    async def run():
        adapter, _, ws = await _connected_adapter()
        # Write a tiny fake opus file
        import tempfile
        fd, path = tempfile.mkstemp(suffix=".opus")
        with os.fdopen(fd, "wb") as fh:
            fh.write(b"\x01\x02\x03opusdata")
        res = await adapter.send_voice("dev42", path)
        assert res.success is True
        assert any(f[0] == "bytes" for f in ws.frames)
        await adapter.disconnect()
    asyncio.run(run())


def test_lifecycle_hooks_push_pet():
    async def run():
        adapter, handler, ws = await _connected_adapter()
        from gateway.platforms.base import MessageEvent, MessageType, ProcessingOutcome
        ev = MessageEvent(
            source=adapter.build_source(chat_id="dev42", user_id="dev42"),
            text="hi", message_type=MessageType.TEXT,
        )
        await adapter.on_processing_start(ev)
        assert any('"state":"run"' in f[1] for f in ws.frames if f[0] == "str")
        await adapter.on_processing_complete(ev, ProcessingOutcome.SUCCESS)
        assert any('"state":"done"' in f[1] for f in ws.frames if f[0] == "str")
        ev2 = MessageEvent(
            source=adapter.build_source(chat_id="dev42", user_id="dev42"),
            text="x", message_type=MessageType.TEXT,
        )
        await adapter.on_processing_complete(ev2, ProcessingOutcome.FAILURE)
        assert any('"state":"error"' in f[1] for f in ws.frames if f[0] == "str")
        await adapter.disconnect()
    asyncio.run(run())


def test_proactive_push():
    async def run():
        adapter, _, ws = await _connected_adapter()
        res = await adapter.push_proactive(
            "dev42", text="Cron job finished", pet_state="done"
        )
        assert res.success is True
        assert any('"state":"done"' in f[1] for f in ws.frames if f[0] == "str")
        assert any("proactive" in f[1] for f in ws.frames if f[0] == "str")
        await adapter.disconnect()
    asyncio.run(run())


def test_io_tool_roundtrip_through_hub():
    async def run():
        adapter, _, _ = await _connected_adapter()
        # Wire a server-side dispatcher so call_tool returns without a device.
        async def dispatch(device_id, tool, params, call_id):
            return {"ok": True, "value": params.get("pin", 0) + 1}
        adapter._hub.set_tool_dispatcher(dispatch)
        out = await adapter.call_io_tool("dev42", "esp_gpio_set", {"pin": 12})
        assert out["ok"] is True
        assert out["value"] == 13
        await adapter.disconnect()
    asyncio.run(run())


def test_get_chat_info():
    async def run():
        adapter, _, _ = await _connected_adapter()
        info = await adapter.get_chat_info("dev42")
        assert info["connected"] is True
        assert info["capabilities"]["imu"] is True
        await adapter.disconnect()
    asyncio.run(run())


def test_offline_send_fails():
    async def run():
        adapter, _, _ = await _connected_adapter()
        res = await adapter.send("nope", "hi")
        assert res.success is False
        assert res.error == "device_offline"
        await adapter.disconnect()
    asyncio.run(run())


if __name__ == "__main__":
    # Allow running directly: minimal runner (no pytest needed).
    import traceback
    funcs = [v for k, v in sorted(globals().items())
             if k.startswith("test_") and callable(v)]
    passed = failed = 0
    for fn in funcs:
        try:
            fn()
            print(f"PASS  {fn.__name__}")
            passed += 1
        except Exception as exc:
            print(f"FAIL  {fn.__name__}: {exc}")
            traceback.print_exc()
            failed += 1
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)
