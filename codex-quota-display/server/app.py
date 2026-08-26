import asyncio
import json
import os
import time
from typing import Any

from fastapi import FastAPI, Header, HTTPException

app = FastAPI(title="Codex Quota Gateway", version="1.0.0")

DEVICE_TOKEN = os.getenv("DEVICE_TOKEN", "CHANGE_ME")
CODEX_BIN = os.getenv("CODEX_BIN", "codex")

_proc: asyncio.subprocess.Process | None = None
_lock = asyncio.Lock()
_next_id = 1


async def _start_codex() -> None:
    global _proc

    if _proc and _proc.returncode is None:
        return

    _proc = await asyncio.create_subprocess_exec(
        CODEX_BIN,
        "app-server",
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    # app-server speaks newline-delimited JSON-RPC over stdio.
    await _rpc_call(
        "initialize",
        {
            "clientInfo": {
                "name": "codex-quota-gateway",
                "version": "1.0.0",
            }
        },
        ensure_started=False,
    )


async def _rpc_call(method: str, params: dict[str, Any] | None = None, *, ensure_started: bool = True) -> Any:
    global _next_id

    if ensure_started:
        await _start_codex()

    if not _proc or not _proc.stdin or not _proc.stdout:
        raise RuntimeError("codex app-server is not available")

    request_id = _next_id
    _next_id += 1

    payload = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method,
        "params": params or {},
    }

    _proc.stdin.write((json.dumps(payload) + "\n").encode("utf-8"))
    await _proc.stdin.drain()

    while True:
        line = await asyncio.wait_for(_proc.stdout.readline(), timeout=10)
        if not line:
            raise RuntimeError("codex app-server exited unexpectedly")

        msg = json.loads(line.decode("utf-8"))

        # Ignore notifications and responses to other requests.
        if msg.get("id") != request_id:
            continue

        if "error" in msg:
            raise RuntimeError(f"Codex RPC error: {msg['error']}")

        return msg.get("result")


async def read_rate_limits() -> dict[str, Any]:
    async with _lock:
        result = await _rpc_call("account/rateLimits/read")

    # Current app-server shape is typically {"rateLimits": {...}}.
    if isinstance(result, dict) and "rateLimits" in result:
        return result["rateLimits"]
    if isinstance(result, dict):
        return result
    raise RuntimeError("unexpected account/rateLimits/read response")


def _remaining_percent(window: dict[str, Any] | None) -> int:
    if not window:
        return 0
    used = float(window.get("usedPercent", 0))
    return max(0, min(100, round(100 - used)))


def _seconds_until_reset(window: dict[str, Any] | None) -> int:
    if not window:
        return 0
    resets_at = window.get("resetsAt")
    if resets_at is None:
        return 0
    return max(0, int(float(resets_at) - time.time()))


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/codex/status")
async def codex_status(x_device_token: str | None = Header(default=None)) -> dict[str, Any]:
    if DEVICE_TOKEN and DEVICE_TOKEN != "CHANGE_ME" and x_device_token != DEVICE_TOKEN:
        raise HTTPException(status_code=401, detail="invalid device token")

    try:
        limits = await read_rate_limits()
    except Exception as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc

    primary = limits.get("primary") or {}
    secondary = limits.get("secondary") or {}

    five_hour_remaining = _remaining_percent(primary)
    weekly_remaining = _remaining_percent(secondary)
    reset_seconds = _seconds_until_reset(primary)

    # READY means the short window currently has usable capacity.
    available = five_hour_remaining > 0 and weekly_remaining > 0

    return {
        "codex_available": available,
        "five_hour_remaining": five_hour_remaining,
        "weekly_remaining": weekly_remaining,
        "five_hour_reset_seconds": reset_seconds,
        "plan_type": limits.get("planType"),
    }
