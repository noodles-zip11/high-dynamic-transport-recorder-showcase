#!/usr/bin/env python3
"""Capture the bounded UART1 FinSH baseline for the G3 preacceptance run."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time

import serial


COMMANDS = ("sysinfo", "ps", "event status", "log status", "log inspect")
FATAL_MARKERS = ("HardFault", "assert", "stack overflow", "watchdog reset")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def digest(path: Path) -> dict[str, int | str]:
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest().upper()}


def read_for(port: serial.Serial, seconds: float) -> bytes:
    deadline = time.monotonic() + seconds
    chunks: list[bytes] = []
    while time.monotonic() < deadline:
        waiting = port.in_waiting
        data = port.read(max(1, min(waiting or 1, 4096)))
        if data:
            chunks.append(bytes(data))
        else:
            time.sleep(0.02)
    return b"".join(chunks)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--label", default="baseline")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = args.output_dir / f"{args.label}.raw.bin"
    console_path = args.output_dir / f"{args.label}.console.log"
    result_path = args.output_dir / f"{args.label}.json"
    raw = bytearray()
    console: list[str] = []
    results: list[dict[str, object]] = []

    def record(direction: str, data: bytes) -> None:
        if not data:
            return
        raw.extend(data)
        text = data.decode("utf-8", errors="replace")
        console.append(f"[{utc_now()}] {direction}\n{text}")

    started = utc_now()
    error: str | None = None
    try:
        with serial.Serial(
            port=args.port,
            baudrate=115200,
            timeout=0.1,
            write_timeout=1.0,
            dsrdtr=False,
            rtscts=False,
            xonxoff=False,
        ) as port:
            port.dtr = False
            port.rts = False
            record("RX_PRE_COMMAND", read_for(port, 1.5))
            for command in COMMANDS:
                payload = (command + "\r\n").encode("ascii")
                port.write(payload)
                record("TX", payload)
                response = read_for(port, 3.0)
                record("RX", response)
                text = response.decode("utf-8", errors="replace")
                results.append(
                    {
                        "command": command,
                        "prompt_seen": "msh >" in text,
                        "response_bytes": len(response),
                        "fatal_marker_seen": any(marker in text for marker in FATAL_MARKERS),
                    }
                )
    except Exception as exc:  # preserve diagnostic evidence and return a failing result
        error = f"{type(exc).__name__}: {exc}"
        console.append(f"[{utc_now()}] ERROR {error}\n")

    raw_path.write_bytes(raw)
    console_path.write_text("\n".join(console), encoding="utf-8", newline="\n")
    passed = (
        error is None
        and len(results) == len(COMMANDS)
        and all(item["prompt_seen"] and not item["fatal_marker_seen"] for item in results)
    )
    result = {
        "schema": "g3-uart1-finsh-baseline-20260821-v1",
        "port": args.port,
        "baud": 115200,
        "format": "8N1",
        "started_at_utc": started,
        "ended_at_utc": utc_now(),
        "power_cycle_performed_by_script": False,
        "timer_started_by_script": False,
        "commands": results,
        "error": error,
        "pass": passed,
        "raw_log": digest(raw_path),
        "console_log": digest(console_path),
    }
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
