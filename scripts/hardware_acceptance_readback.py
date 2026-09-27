#!/usr/bin/env python3
"""Collect read-only UART3 evidence for the G2/G3 hardware gates.

The collector sends only HELLO, GET_DEVICE_INFO, GET_HEALTH, LIST_EVENTS,
MODEL_OTA_QUERY and optional event readback requests. It never starts an OTA,
formats storage, writes Flash/QSPI, resets the board, or changes time.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "host"))

from transport_recorder.protocol.client import TerpClient  # noqa: E402
from transport_recorder.protocol.serial_transport import SerialTransport  # noqa: E402


DEFAULT_IDENTITY = {
    "model": "STM32H743",
    "firmware_version": "phase08-terp-uart3",
    "hardware_version": "openmv4-h743-pd8-pd9",
    "serial_number": "recorder-001",
    "capability_flags": 235,
}


def timestamp() -> str:
    return datetime.now(timezone.utc).isoformat()


class RecordingTransport:
    """Record raw I/O while preserving the SerialTransport interface."""

    def __init__(self, inner: SerialTransport, raw_path: Path) -> None:
        self._inner = inner
        self._raw = raw_path.open("w", encoding="utf-8", newline="\n")

    def _record(self, direction: str, data: bytes) -> None:
        self._raw.write(json.dumps({
            "timestamp_utc": timestamp(),
            "direction": direction,
            "bytes": len(data),
            "hex": data.hex().upper(),
        }, ensure_ascii=False, separators=(",", ":")) + "\n")
        self._raw.flush()

    def write(self, data: bytes) -> None:
        self._record("TX", data)
        self._inner.write(data)

    def read(self, maximum_bytes: int) -> bytes:
        data = self._inner.read(maximum_bytes)
        if data:
            self._record("RX", data)
        return data

    def close(self) -> None:
        try:
            self._inner.close()
        finally:
            self._raw.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--label", default="readback")
    parser.add_argument("--event-id", type=int,
                        help="Optionally download and verify one existing event")
    parser.add_argument("--require-ai-result", action="store_true",
                        help="Fail if GET_AI_RESULT for --event-id is unavailable")
    parser.add_argument("--timeout", type=float, default=2.0)
    return parser.parse_args()


def list_all_events(client: TerpClient) -> list[Any]:
    events: list[Any] = []
    after_event_id = 0
    for _ in range(256):
        page, next_event_id = client.list_events(after_event_id, 16)
        events.extend(page)
        if next_event_id == 0:
            return events
        if next_event_id == after_event_id:
            raise RuntimeError("LIST_EVENTS cursor did not advance")
        after_event_id = next_event_id
    raise RuntimeError("LIST_EVENTS exceeded 256 pages")


def digest(path: Path) -> dict[str, int | str]:
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest().upper()}


def run() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = args.output_dir / f"{args.label}.raw.jsonl"
    frames_path = args.output_dir / f"{args.label}.frames.jsonl"
    result_path = args.output_dir / f"{args.label}.json"
    frames_file = frames_path.open("w", encoding="utf-8", newline="\n")
    transport: RecordingTransport | None = None
    result: dict[str, Any] = {
        "schema": "hardware-acceptance-readback-v1",
        "label": args.label,
        "port": args.port,
        "started_at_utc": timestamp(),
        "read_only": True,
        "pass": False,
    }
    try:
        serial_transport = SerialTransport.open(
            args.port, baudrate=115200, timeout_seconds=0.2
        )
        transport = RecordingTransport(serial_transport, raw_path)
        client = TerpClient(transport, timeout_seconds=args.timeout)
        client.trace = lambda frame: frames_file.write(
            json.dumps({"timestamp_utc": timestamp(), **asdict(frame)},
                       ensure_ascii=False, separators=(",", ":")) + "\n"
        )

        info = client.hello()
        health = client.get_health()
        events = list_all_events(client)
        model = client.model_ota_query()
        device_info = asdict(info)
        health_info = asdict(health)
        model_info = asdict(model)
        event_ids = [event.event_id for event in events]
        identity_match = device_info == DEFAULT_IDENTITY
        health_ok = (
            health.storage_ready
            and health.storage_error_count == 0
            and health.event_export_error_count == 0
        )
        model_ok = model.model_valid and not model.pending_install
        result.update({
            "device_info": device_info,
            "health": health_info,
            "model_ota_query": model_info,
            "event_count": len(events),
            "event_ids": event_ids,
            "identity_match": identity_match,
            "health_ok": health_ok,
            "model_ok": model_ok,
        })

        event_ok = True
        if args.event_id is not None:
            event_ok = args.event_id in event_ids
            event_data: dict[str, Any] = {"event_id": args.event_id, "listed": event_ok}
            if event_ok:
                event_info = client.get_event_info(args.event_id)
                event_path = args.output_dir / f"event-{args.event_id}.terp-event"
                client.download_event(args.event_id, event_path)
                event_data["event_info"] = asdict(event_info)
                event_data["download"] = digest(event_path)
                try:
                    event_data["ai_result"] = asdict(client.get_ai_result(args.event_id))
                except Exception as error:  # diagnostic evidence, not silent success
                    event_data["ai_result_error"] = f"{type(error).__name__}: {error}"
                    if args.require_ai_result:
                        event_ok = False
            result["event_readback"] = event_data
        result["event_readback_ok"] = event_ok
        result["pass"] = bool(identity_match and health_ok and model_ok and events and event_ok)
    except Exception as error:
        result["error"] = f"{type(error).__name__}: {error}"
    finally:
        frames_file.close()
        if transport is not None:
            transport.close()
        result["ended_at_utc"] = timestamp()
        result["raw_log"] = digest(raw_path) if raw_path.exists() else None
        result["frame_summary"] = digest(frames_path) if frames_path.exists() else None
        result_path.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(run())
