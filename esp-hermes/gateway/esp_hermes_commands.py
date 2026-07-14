"""
esp_hermes_commands.py — ESP-Hermes slash-command engine (spec §18)
====================================================================

Parses the ESP device's own command surface *before* the agent loop, mirroring
Telegram slash routing (`gateway/run.py` -> `gateway/slash_commands.py`).

Input methods (spec §18):
  * Voice:     "Hermes, Befehl <x>" -> STT -> text -> parsed as command
  * Serial:    typed over USB/BLE companion app
  * Gesture:   long-press + shake -> command palette on LCD

Command set (spec §18):
  /mode <ptt|vad>        switch input mode
  /pet <slug>            change pet (uses `hermes pets`)
  /display <pet|tui>     toggle LCD mode
  /mute  /unmute         audio cues on/off
  /status                battery, wifi, session
  /sleep                 deep-sleep now
  /tasks                 list active cron/agent tasks
  /notify <msg>          push note to another channel (Telegram)
  /gpio <pin> <H|L>      direct IO (allowlist + approval, spec §8)
  /i2c <addr> <reg>      read sensor (allowlist)
  /reset                 clear device session
  /help                  list commands on LCD

The engine is **self-contained**: it depends only on stdlib, so it is
unit-testable without the full gateway (mirrors esp_hermes_ws.py's design).
The :class:`EspHermesAdapter` owns an :class:`EspHermesCommands` instance and
calls :meth:`EspHermesCommands.maybe_handle` from its inbound path. IO commands
are gated by the per-device allowlist (spec §8) with optional approval,
rate-limiting, and audit logging (also spec §8 / §17).

Why a separate engine and not the gateway's slash_commands mixin?
  * The ESP command surface is *device-scoped* — its semantics (mode, pet,
    display, GPIO allowlist) are entirely ESP-specific and do not belong in the
    generic in-session `/model` `/reset` machinery.
  * The engine must run on the **inbound** path (device -> gateway) and return a
    structured :class:`CommandResult` so the adapter can render it on the tiny
    LCD / as a TTS line, which is a different contract than the user-facing
    EphemeralReply handlers.
  * Keeping it standalone lets both the WS hub tests and the adapter tests
    exercise parse/allowlist/audit logic without booting a gateway runner.

Protocol message shapes: inbound ``{"type": "text", "text": "/mode vad"}``
(spec §18) or ``{"type": "command", "name": ..., "args": ...}``. Outbound
command replies use ``{"type": "command_reply", "command": ..., "ok": ...}``.
"""

from __future__ import annotations

import logging
import os
import re
import time
from dataclasses import dataclass, field
from typing import Any, Awaitable, Callable, Dict, List, Optional

logger = logging.getLogger("esp_hermes.commands")


# ---------------------------------------------------------------------------
# Command registry metadata (spec §18)
# ---------------------------------------------------------------------------
# Each entry: name -> dict with help + whether it touches hardware (IO).
_COMMAND_META: Dict[str, Dict[str, Any]] = {
    "mode":     {"help": "/mode ptt|vad — switch input mode", "io": False},
    "pet":      {"help": "/pet <slug> — change LCD pet", "io": False},
    "display":  {"help": "/display pet|tui — toggle LCD mode", "io": False},
    "mute":     {"help": "/mute — silence audio cues", "io": False},
    "unmute":   {"help": "/unmute — enable audio cues", "io": False},
    "status":   {"help": "/status — battery, wifi, session", "io": False},
    "sleep":    {"help": "/sleep — deep-sleep now", "io": False},
    "tasks":    {"help": "/tasks — list active cron/agent tasks", "io": False},
    "notify":   {"help": "/notify <msg> — push to Telegram", "io": False},
    "gpio":     {"help": "/gpio <pin> <H|L> — direct IO (allowlist)", "io": True},
    "i2c":      {"help": "/i2c <addr> <reg> — read sensor (allowlist)", "io": True},
    "reset":    {"help": "/reset — clear device session", "io": False},
    "help":     {"help": "/help — list commands", "io": False},
}
_VALID_COMMANDS = frozenset(_COMMAND_META.keys())

# Pet-state / display modes share vocabulary with the LCD renderer.
_VALID_MODES = frozenset({"ptt", "vad"})
_VALID_DISPLAYS = frozenset({"pet", "tui"})

# Hard-blocked pins (spec §8): boot/flash/UART0 must never be exposed even if a
# device's allowlist accidentally lists them. Firmware also hard-blocks these.
HARD_BLOCKED_PINS = frozenset({0, 1, 2, 3})

# Power pins are treated as destructive regardless of allowlist (spec §8).
_POWER_PINS = frozenset({0, 1, 2, 3})  # overlap with hard-blocked by default

# Audit log destination (spec §8 / §17).
_AUDIT_PATH = os.path.join(
    os.path.expanduser("~/.hermes/logs"), "esp_hermes.log"
)


@dataclass
class CommandResult:
    """Structured outcome of a command, rendered by the adapter on LCD/TTS."""
    command: str
    ok: bool
    message: str
    # Optional outbound protocol frame the adapter should push to the device.
    reply_frame: Optional[Dict[str, Any]] = None
    # When True the command was an IO op that requires interactive approval
    # (spec §8). The adapter surfaces it to the user; Hermes does NOT auto-run.
    needs_approval: bool = False
    # Side-effect hint for the adapter (e.g. set mode, push pet, deep-sleep).
    action: Optional[str] = None
    # Arbitrary data the adapter/agent may consume (e.g. mode, pet slug).
    data: Dict[str, Any] = field(default_factory=dict)


@dataclass
class DeviceConfig:
    """
    Per-device safety + behaviour config (spec §8). Mirrors the YAML schema in
    config/esp_hermes.yaml. Populated from config by the adapter at runtime.
    """
    allowed_pins: List[int] = field(default_factory=list)
    allowed_i2c: List[int] = field(default_factory=list)
    blocked_pins: List[int] = field(default_factory=list)
    auto_approve_safe: bool = False
    rate_limit: Dict[str, str] = field(
        default_factory=lambda: {"gpio_set": "5/s", "pwm_set": "5/s"}
    )
    audio_cues: bool = True
    # Runtime state (mutable, adapter-owned).
    mode: str = "ptt"
    pet_slug: str = "default"
    display: str = "pet"
    muted: bool = False


def _audit(device_id: str, action: str, detail: str = "") -> None:
    """Append one line to the esp_hermes audit log (spec §8 / §17)."""
    try:
        os.makedirs(os.path.dirname(_AUDIT_PATH), exist_ok=True)
        ts = time.strftime("%Y-%m-%d %H:%M:%S")
        line = f"{ts} AUDIT device={device_id} action={action} {detail}\n"
        with open(_AUDIT_PATH, "a", encoding="utf-8") as fh:
            fh.write(line)
    except OSError:
        # Audit failure must never break a command; log to stderr via logger.
        try:
            logger.warning("esp_hermes audit write failed: %s", _AUDIT_PATH)
        except Exception:
            pass


def _parse_rate(rate: str) -> (int, float):
    """Parse '5/s' -> (5, 1.0) or '10/30s' -> (10, 30.0). Default (1, 1.0)."""
    m = re.match(r"\s*(\d+)\s*/\s*(\d+(?:\.\d+)?)\s*([smh]?)\s*$", rate or "")
    if not m:
        return 1, 1.0
    count = int(m.group(1))
    window = float(m.group(2))
    unit = m.group(3)
    if unit == "m":
        window *= 60.0
    elif unit == "h":
        window *= 3600.0
    return count, window


class _RateLimiter:
    """Simple sliding-window rate limiter keyed by (device, kind)."""

    def __init__(self) -> None:
        # key -> list of timestamps
        self._hits: Dict[str, List[float]] = {}

    def allow(self, key: str, limit: int, window: float,
              now: Optional[float] = None) -> bool:
        now = now if now is not None else time.monotonic()
        hits = self._hits.setdefault(key, [])
        # drop stale
        cutoff = now - window
        hits[:] = [t for t in hits if t > cutoff]
        if len(hits) >= limit:
            return False
        hits.append(now)
        return True

    def reset(self, key: str) -> None:
        self._hits.pop(key, None)


class EspHermesCommands:
    """
    Parse + dispatch ESP slash-commands *before* the agent loop.

    The adapter constructs one engine per device (or one shared engine that is
    told the device id per call — both work; we keep device config in a dict so
    a single engine can serve many devices like the WS hub does).

    IO commands (/gpio, /i2c) are gated by the device allowlist (spec §8) with
    approval + rate-limit + audit. Non-IO commands mutate device state / push
    frames the adapter renders.
    """

    def __init__(
        self,
        configs: Optional[Dict[str, DeviceConfig]] = None,
        io_executor: Optional[Callable[[str, str, Dict[str, Any]], Awaitable[Dict[str, Any]]]] = None,
        notify_sender: Optional[Callable[[str, str], Awaitable[Any]]] = None,
        status_provider: Optional[Callable[[str], Awaitable[Dict[str, Any]]]] = None,
        tasks_provider: Optional[Callable[[], Awaitable[List[Dict[str, Any]]]]] = None,
        pet_resolver: Optional[Callable[[str], Awaitable[Optional[str]]]] = None,
    ) -> None:
        """
        Args:
          configs:        per-device DeviceConfig map (mutable; engine updates
                          runtime state like mode/pet/display in place).
          io_executor:    async callable(device_id, tool, params) -> result dict;
                          performs the real IO roundtrip (adapter wires the hub).
                          If None, IO commands still validate against the
                          allowlist but report "io_executor_unavailable".
          notify_sender:  async callable(device_id, message) -> pushes to Telegram.
          status_provider: async callable(device_id) -> {battery, wifi, session}.
          tasks_provider:  async callable() -> list of active task dicts.
          pet_resolver:    async callable(slug) -> resolved slug or None (hermes pets).
        """
        self._configs: Dict[str, DeviceConfig] = configs or {}
        self._io_executor = io_executor
        self._notify_sender = notify_sender
        self._status_provider = status_provider
        self._tasks_provider = tasks_provider
        self._pet_resolver = pet_resolver
        self._ratelimit = _RateLimiter()

    # -- device config access ------------------------------------------------
    def get_config(self, device_id: str) -> DeviceConfig:
        cfg = self._configs.get(device_id)
        if cfg is None:
            cfg = DeviceConfig()
            self._configs[device_id] = cfg
        return cfg

    def set_config(self, device_id: str, cfg: DeviceConfig) -> None:
        self._configs[device_id] = cfg

    # -- entry point ---------------------------------------------------------
    def parse(self, text: str) -> Optional[tuple[str, str]]:
        """
        Extract (command, args) from raw inbound text, or None if not a command.

        Recognizes:
          * "/cmd args"           -> command surface
          * "Hermes, Befehl x"    -> voice-STT command prefix (spec §18)
          * "hermes command x"    -> loose English alias
          * already-structured {"type":"command","name":...,"args":...} handled
            by the adapter before calling dispatch().
        Strips an optional "@bot" suffix (like Telegram commands).
        """
        if not text:
            return None
        t = text.strip()
        if not t:
            return None

        # Voice/STT command prefix (spec §18 "Hermes, Befehl <x>")
        m = re.match(
            r"^(?:hermes[,]?\s*(?:befehl|command)|hermes\s+command)\s+(.+)$",
            t, re.IGNORECASE)
        if m:
            t = m.group(1).strip()

        if not t.startswith("/"):
            return None
        body = t[1:].strip()
        # drop @bot suffix
        body = re.sub(r"@\S+\s*$", "", body).strip()
        if not body:
            return None
        parts = body.split(maxsplit=1)
        cmd = parts[0].lower()
        args = parts[1].strip() if len(parts) > 1 else ""
        if cmd not in _VALID_COMMANDS:
            return None
        return cmd, args

    async def maybe_handle(self, device_id: str, text: str) -> Optional[CommandResult]:
        """
        If *text* is an ESP command, parse + dispatch it and return the result.
        Returns None when the text is NOT a command (so the adapter falls
        through to the normal agent loop / STT path).

        The adapter calls this on the inbound path *before* building a
        MessageEvent for the agent (spec §18: "commands parse ... before agent
        loop").
        """
        parsed = self.parse(text)
        if parsed is None:
            return None
        cmd, args = parsed
        return await self.dispatch(device_id, cmd, args)

    async def dispatch(self, device_id: str, cmd: str,
                        args: str) -> CommandResult:
        """Dispatch a known command (cmd already validated as a valid name)."""
        handler = getattr(self, f"_cmd_{cmd}", None)
        if handler is None:
            return CommandResult(
                command=cmd, ok=False, message=f"unknown command: {cmd}")
        try:
            return await handler(device_id, args)
        except Exception as exc:  # never let a command crash the inbound loop
            logger.exception("esp_hermes command %s dev=%s failed", cmd, device_id)
            return CommandResult(
                command=cmd, ok=False,
                message=f"command error: {exc}")

    # =====================================================================
    # Non-IO commands
    # =====================================================================
    async def _cmd_mode(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        mode = (args or "").strip().lower()
        if mode not in _VALID_MODES:
            return CommandResult(
                command="mode", ok=False,
                message="usage: /mode ptt|vad",
                reply_frame={"type": "command_reply", "command": "mode",
                             "ok": False, "message": "usage: /mode ptt|vad"})
        cfg.mode = mode
        _audit(device_id, "mode", f"mode={mode}")
        return CommandResult(
            command="mode", ok=True, message=f"mode -> {mode}",
            action="mode", data={"mode": mode},
            reply_frame={"type": "mode_ack", "mode": mode})

    async def _cmd_pet(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        slug = (args or "").strip()
        if not slug:
            return CommandResult(
                command="pet", ok=False, message="usage: /pet <slug>")
        # Resolve via `hermes pets` if a resolver is wired; else accept as-is.
        resolved = slug
        if self._pet_resolver is not None:
            try:
                got = await self._pet_resolver(slug)
            except Exception:
                got = None
            if got is None:
                return CommandResult(
                    command="pet", ok=False,
                    message=f"unknown pet: {slug}",
                    reply_frame={"type": "command_reply", "command": "pet",
                                 "ok": False, "message": f"unknown pet: {slug}"})
            resolved = got
        cfg.pet_slug = resolved
        _audit(device_id, "pet", f"slug={resolved}")
        return CommandResult(
            command="pet", ok=True, message=f"pet -> {resolved}",
            action="pet", data={"slug": resolved})

    async def _cmd_display(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        disp = (args or "").strip().lower()
        if disp not in _VALID_DISPLAYS:
            return CommandResult(
                command="display", ok=False,
                message="usage: /display pet|tui")
        cfg.display = disp
        _audit(device_id, "display", f"display={disp}")
        return CommandResult(
            command="display", ok=True, message=f"display -> {disp}",
            action="display", data={"display": disp})

    async def _cmd_mute(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        cfg.muted = True
        _audit(device_id, "mute")
        return CommandResult(
            command="mute", ok=True, message="audio cues muted",
            action="mute", data={"muted": True})

    async def _cmd_unmute(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        cfg.muted = False
        _audit(device_id, "unmute")
        return CommandResult(
            command="unmute", ok=True, message="audio cues on",
            action="unmute", data={"muted": False})

    async def _cmd_status(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        info: Dict[str, Any] = {
            "mode": cfg.mode, "pet": cfg.pet_slug,
            "display": cfg.display, "muted": cfg.muted,
        }
        if self._status_provider is not None:
            try:
                info.update(await self._status_provider(device_id))
            except Exception as exc:
                info["status_error"] = str(exc)
        lines = ", ".join(f"{k}={v}" for k, v in info.items())
        return CommandResult(
            command="status", ok=True, message=lines, data=info,
            reply_frame={"type": "tui_line", "role": "agent",
                         "text": f"status: {lines}"})

    async def _cmd_sleep(self, device_id: str, args: str) -> CommandResult:
        _audit(device_id, "sleep")
        return CommandResult(
            command="sleep", ok=True, message="deep-sleep now",
            action="sleep",
            reply_frame={"type": "sleep"})

    async def _cmd_tasks(self, device_id: str, args: str) -> CommandResult:
        tasks: List[Dict[str, Any]] = []
        if self._tasks_provider is not None:
            try:
                tasks = await self._tasks_provider()
            except Exception as exc:
                return CommandResult(
                    command="tasks", ok=False,
                    message=f"tasks unavailable: {exc}")
        if not tasks:
            return CommandResult(
                command="tasks", ok=True, message="no active tasks",
                reply_frame={"type": "tui_line", "role": "agent",
                             "text": "tasks: none active"})
        # Keep it LCD-short: list up to 5 titles.
        items = [t.get("title") or t.get("id") or "?" for t in tasks[:5]]
        msg = "; ".join(items)
        return CommandResult(
            command="tasks", ok=True, message=msg, data={"tasks": tasks},
            reply_frame={"type": "tui_line", "role": "agent",
                         "text": f"tasks: {msg}"})

    async def _cmd_notify(self, device_id: str, args: str) -> CommandResult:
        msg = (args or "").strip()
        if not msg:
            return CommandResult(
                command="notify", ok=False, message="usage: /notify <msg>")
        if self._notify_sender is None:
            return CommandResult(
                command="notify", ok=False,
                message="notify target not configured (Telegram)")
        try:
            await self._notify_sender(device_id, msg)
        except Exception as exc:
            return CommandResult(
                command="notify", ok=False,
                message=f"notify failed: {exc}")
        _audit(device_id, "notify", f"msg_len={len(msg)}")
        return CommandResult(
            command="notify", ok=True, message="notified Telegram")

    async def _cmd_reset(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        # Reset runtime state (session is cleared by the adapter elsewhere).
        cfg.mode = "ptt"
        cfg.pet_slug = "default"
        cfg.display = "pet"
        cfg.muted = False
        self._ratelimit.reset(f"{device_id}:gpio_set")
        self._ratelimit.reset(f"{device_id}:i2c_read")
        _audit(device_id, "reset")
        return CommandResult(
            command="reset", ok=True, message="device session cleared",
            action="reset")

    async def _cmd_help(self, device_id: str, args: str) -> CommandResult:
        lines = [m["help"] for m in _COMMAND_META.values()]
        help_text = "\n".join(lines)
        return CommandResult(
            command="help", ok=True, message=help_text,
            reply_frame={"type": "help", "commands": lines})

    # =====================================================================
    # IO commands (allowlist + approval + rate-limit + audit, spec §8)
    # =====================================================================
    async def _cmd_gpio(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        parts = args.split()
        if len(parts) != 2:
            return CommandResult(
                command="gpio", ok=False, message="usage: /gpio <pin> <H|L>")
        try:
            pin = int(parts[0])
        except ValueError:
            return CommandResult(
                command="gpio", ok=False, message="pin must be an integer")
        level = parts[1].strip().upper()
        if level not in ("H", "HIGH", "L", "LOW"):
            return CommandResult(
                command="gpio", ok=False, message="level must be H or L")
        state = "HIGH" if level in ("H", "HIGH") else "LOW"

        # --- safety gates (spec §8) ---
        gate = self._check_pin_allowed(cfg, pin, device_id)
        if gate is not None:
            return gate

        # Rate-limit (spec §8 rate_limit gpio_set)
        limit, window = _parse_rate(cfg.rate_limit.get("gpio_set", "5/s"))
        if not self._ratelimit.allow(f"{device_id}:gpio_set", limit, window):
            _audit(device_id, "gpio_denied", f"pin={pin} reason=rate_limit")
            return CommandResult(
                command="gpio", ok=False,
                message="rate limit exceeded for /gpio",
                reply_frame={"type": "command_reply", "command": "gpio",
                             "ok": False, "message": "rate limited"})

        # Destructive / power pin -> require approval (spec §8 approval)
        destructive = pin in _POWER_PINS
        if destructive and not cfg.auto_approve_safe:
            _audit(device_id, "gpio_pending", f"pin={pin} state={state} reason=approval")
            return CommandResult(
                command="gpio", ok=False, needs_approval=True,
                message=f"approve /gpio {pin} {state}?",
                action="approve_gpio",
                data={"pin": pin, "state": state})

        # Execute
        _audit(device_id, "gpio", f"pin={pin} state={state}")
        return await self._run_io(device_id, "esp_gpio_set",
                                  {"pin": pin, "state": state},
                                  CommandResult(command="gpio", ok=True,
                                                message=f"gpio {pin} -> {state}",
                                                data={"pin": pin, "state": state}))

    async def _cmd_i2c(self, device_id: str, args: str) -> CommandResult:
        cfg = self.get_config(device_id)
        parts = args.split()
        if len(parts) < 2:
            return CommandResult(
                command="i2c", ok=False, message="usage: /i2c <addr> <reg> [len]")
        try:
            addr = int(parts[0], 0)  # accept 0x40 / 64
            reg = int(parts[1], 0)
        except ValueError:
            return CommandResult(
                command="i2c", ok=False,
                message="addr/reg must be integers (0x40 or 64)")
        length = 1
        if len(parts) >= 3:
            try:
                length = int(parts[2])
            except ValueError:
                return CommandResult(
                    command="i2c", ok=False, message="len must be an integer")

        # --- allowlist gate (spec §8 allowed_i2c) ---
        if cfg.allowed_i2c and addr not in cfg.allowed_i2c:
            _audit(device_id, "i2c_denied", f"addr={hex(addr)} reason=allowlist")
            return CommandResult(
                command="i2c", ok=False,
                message=f"i2c addr {hex(addr)} not in allowlist",
                reply_frame={"type": "command_reply", "command": "i2c",
                             "ok": False,
                             "message": f"addr {hex(addr)} not allowed"})

        # Rate-limit
        limit, window = _parse_rate(cfg.rate_limit.get("i2c_read", "5/s"))
        if not self._ratelimit.allow(f"{device_id}:i2c_read", limit, window):
            _audit(device_id, "i2c_denied", f"addr={hex(addr)} reason=rate_limit")
            return CommandResult(
                command="i2c", ok=False, message="rate limit exceeded for /i2c")

        _audit(device_id, "i2c", f"addr={hex(addr)} reg={reg} len={length}")
        return await self._run_io(
            device_id, "esp_i2c_read",
            {"addr": addr, "reg": reg, "len": length},
            CommandResult(command="i2c", ok=True,
                          message=f"i2c {hex(addr)}.{reg} read",
                          data={"addr": addr, "reg": reg, "len": length}))

    # -- shared IO helpers ---------------------------------------------------
    def _check_pin_allowed(self, cfg: DeviceConfig, pin: int,
                           device_id: str) -> Optional[CommandResult]:
        """Return a CommandResult (denied) or None if allowed."""
        if pin in HARD_BLOCKED_PINS:
            _audit(device_id, "gpio_denied", f"pin={pin} reason=hard_blocked")
            return CommandResult(
                command="gpio", ok=False,
                message=f"pin {pin} is hard-blocked (boot/flash/UART0)",
                reply_frame={"type": "command_reply", "command": "gpio",
                             "ok": False,
                             "message": f"pin {pin} hard-blocked"})
        if cfg.blocked_pins and pin in cfg.blocked_pins:
            _audit(device_id, "gpio_denied", f"pin={pin} reason=blocked")
            return CommandResult(
                command="gpio", ok=False,
                message=f"pin {pin} is blocked for this device",
                reply_frame={"type": "command_reply", "command": "gpio",
                             "ok": False, "message": f"pin {pin} blocked"})
        if cfg.allowed_pins and pin not in cfg.allowed_pins:
            _audit(device_id, "gpio_denied", f"pin={pin} reason=allowlist")
            return CommandResult(
                command="gpio", ok=False,
                message=f"pin {pin} not in allowlist",
                reply_frame={"type": "command_reply", "command": "gpio",
                             "ok": False,
                             "message": f"pin {pin} not allowed"})
        return None

    async def _run_io(self, device_id: str, tool: str, params: Dict[str, Any],
                      ok_result: CommandResult) -> CommandResult:
        """Execute an allowed IO op via the wired executor; fall back cleanly."""
        if self._io_executor is None:
            return CommandResult(
                command=ok_result.command, ok=False,
                message="io executor unavailable (hub not connected)",
                reply_frame={"type": "command_reply",
                             "command": ok_result.command,
                             "ok": False, "message": "io unavailable"})
        try:
            res = await self._io_executor(device_id, tool, params)
        except Exception as exc:
            return CommandResult(
                command=ok_result.command, ok=False,
                message=f"io error: {exc}")
        if not res.get("ok"):
            return CommandResult(
                command=ok_result.command, ok=False,
                message=f"io failed: {res.get('error', 'unknown')}",
                data=res)
        # Merge device value into the success message where meaningful.
        value = res.get("value")
        if value is not None:
            ok_result.message = f"{ok_result.message} = {value}"
            ok_result.data["value"] = value
        ok_result.reply_frame = {
            "type": "command_reply", "command": ok_result.command,
            "ok": True, "value": value,
        }
        return ok_result

    # -- approval resolution (spec §8) --------------------------------------
    async def approve(self, device_id: str, pending: CommandResult,
                      granted: bool) -> CommandResult:
        """
        Resolve a `needs_approval` IO command. If granted, re-run the IO op
        (allowlist already passed at parse time); if denied, record + abort.
        """
        if not pending.needs_approval:
            return pending
        if not granted:
            _audit(device_id, "gpio_denied", "reason=user_denied")
            return CommandResult(
                command=pending.command, ok=False, message="denied by user",
                reply_frame={"type": "command_reply", "command": pending.command,
                             "ok": False, "message": "denied"})
        action = pending.action
        data = pending.data
        if action == "approve_gpio":
            cfg = self.get_config(device_id)
            cfg.auto_approve_safe = False  # one-shot approval, not permanent
            _audit(device_id, "gpio_approved",
                   f"pin={data.get('pin')} state={data.get('state')}")
            return await self._run_io(
                device_id, "esp_gpio_set",
                {"pin": data["pin"], "state": data["state"]},
                CommandResult(command="gpio", ok=True,
                              message=f"gpio {data['pin']} -> {data['state']}",
                              data=data))
        return CommandResult(
            command=pending.command, ok=False,
            message="unknown approval target")

    # -- convenience for adapter --------------------------------------------
    @staticmethod
    def help_lines() -> List[str]:
        return [m["help"] for m in _COMMAND_META.values()]


# Backwards-friendly alias used by some callers/tests.
EspHermesCommandEngine = EspHermesCommands
