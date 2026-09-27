#!/usr/bin/env python3
"""Run the scoped hardware regression for sample-pool export backpressure."""

from __future__ import annotations

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
import time
from typing import Any

import serial


REPO_ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(REPO_ROOT / "host"))

from transport_recorder.protocol.client import TerpClient  # noqa: E402
from transport_recorder.protocol.serial_transport import SerialTransport  # noqa: E402


COUNTER_KEYS = (
    "fifo_count_err",
    "fifo_capacity_err",
    "fifo_read_err",
    "fifo_parse_err",
    "dma_start_err",
    "dma_timeout",
    "dma_complete_err",
    "pool_backpressure",
    "export_err",
    "resource_err",
    "queue_drop",
    "result_queue_drop",
    "store_err",
    "feature_err",
    "runtime_err",
)
FATAL_MARKERS = ("HardFault", "assertion", "stack overflow", "watchdog reset")
METRIC_PATTERN = re.compile(r"\b([A-Za-z0-9_]+)=(\d+)\b")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def digest(path: Path) -> dict[str, int | str]:
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest().upper()}


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, default=str) + "\n",
        encoding="utf-8",
    )


def extract_metrics(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for key, value_text in METRIC_PATTERN.findall(text):
        if key not in COUNTER_KEYS and key != "pool_min_free":
            continue
        value = int(value_text)
        if key == "pool_min_free":
            values[key] = min(values.get(key, value), value)
        else:
            values[key] = max(values.get(key, 0), value)
    return values


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


class FinshSession:
    """Capture UART1 command traffic without toggling power or control lines."""

    def __init__(self, port_name: str, output_dir: Path) -> None:
        output_dir.mkdir(parents=True, exist_ok=True)
        self.raw_path = output_dir / "runtime.raw.bin"
        self.console_path = output_dir / "runtime.console.log"
        self._raw = self.raw_path.open("ab")
        self._console = self.console_path.open("a", encoding="utf-8", newline="\n")
        self._port = serial.Serial(
            port=port_name,
            baudrate=115200,
            timeout=0.1,
            write_timeout=1.0,
            dsrdtr=False,
            rtscts=False,
            xonxoff=False,
        )
        self._port.dtr = False
        self._port.rts = False
        self.records: list[dict[str, Any]] = []

    def _record_bytes(self, direction: str, data: bytes) -> None:
        if not data:
            return
        self._raw.write(data)
        self._raw.flush()
        self._console.write(f"[{utc_now()}] {direction}\n")
        self._console.write(data.decode("utf-8", errors="replace"))
        self._console.write("\n")
        self._console.flush()

    def _read_for(self, seconds: float) -> bytes:
        deadline = time.monotonic() + seconds
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            waiting = self._port.in_waiting
            data = self._port.read(max(1, min(waiting or 1, 4096)))
            if data:
                chunks.append(bytes(data))
            else:
                time.sleep(0.02)
        return b"".join(chunks)

    def command(self, command: str, wait_seconds: float = 2.0) -> dict[str, Any]:
        self._record_bytes("RX_PRE_COMMAND", self._read_for(0.2))
        payload = (command + "\r\n").encode("ascii")
        self._port.write(payload)
        self._record_bytes("TX", payload)
        response = self._read_for(wait_seconds)
        self._record_bytes("RX", response)
        text = response.decode("utf-8", errors="replace")
        record: dict[str, Any] = {
            "timestamp_utc": utc_now(),
            "command": command,
            "response_bytes": len(response),
            "prompt_seen": "msh >" in text,
            "fatal_marker_seen": any(
                marker.lower() in text.lower() for marker in FATAL_MARKERS
            ),
            "metrics": extract_metrics(text),
            "text": text,
        }
        self.records.append(record)
        return record

    def close(self) -> None:
        try:
            if self._port.is_open:
                self._port.close()
        finally:
            self._raw.close()
            self._console.close()


class RecordingTransport:
    """Record UART3 TERP frames while preserving the transport interface."""

    def __init__(self, inner: SerialTransport, raw_path: Path) -> None:
        raw_path.parent.mkdir(parents=True, exist_ok=True)
        self._inner = inner
        self._raw = raw_path.open("a", encoding="utf-8", newline="\n")

    def _record(self, direction: str, data: bytes) -> None:
        self._raw.write(
            json.dumps(
                {
                    "timestamp_utc": utc_now(),
                    "direction": direction,
                    "bytes": len(data),
                    "hex": data.hex().upper(),
                },
                separators=(",", ":"),
            )
            + "\n"
        )
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


def capture_event(
    client: TerpClient, event_id: int, output_dir: Path
) -> dict[str, Any]:
    result: dict[str, Any] = {"event_id": event_id, "started_at_utc": utc_now()}
    try:
        info = client.get_event_info(event_id)
        path = output_dir / f"event-{event_id}.terp-event"
        client.download_event(event_id, path)
        result.update(
            {
                "event_info": asdict(info),
                "download": digest(path),
                "pass": True,
            }
        )
        try:
            result["ai_result"] = asdict(client.get_ai_result(event_id))
        except Exception as error:  # AI is supplementary to this pool regression.
            result["ai_result_error"] = f"{type(error).__name__}: {error}"
    except Exception as error:
        result["error"] = f"{type(error).__name__}: {error}"
        result["pass"] = False
    result["ended_at_utc"] = utc_now()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uart1", required=True)
    parser.add_argument("--uart3", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--duration-seconds", type=float, default=360.0)
    parser.add_argument("--heartbeat-seconds", type=float, default=15.0)
    parser.add_argument("--trigger-offsets", default="20,140,260")
    args = parser.parse_args()
    if args.duration_seconds <= 0 or args.heartbeat_seconds <= 0:
        raise SystemExit("duration and heartbeat must be positive")
    trigger_offsets = [
        float(value.strip()) for value in args.trigger_offsets.split(",") if value.strip()
    ]
    if any(offset < 0 or offset >= args.duration_seconds for offset in trigger_offsets):
        raise SystemExit("trigger offsets must be within the requested duration")

    output_dir = args.output_dir
    uart1_dir = output_dir / "uart1"
    uart3_dir = output_dir / "uart3"
    event_dir = output_dir / "event-readback"
    heartbeat_dir = output_dir / "heartbeats"
    for path in (uart1_dir, uart3_dir, event_dir, heartbeat_dir):
        path.mkdir(parents=True, exist_ok=True)

    state: dict[str, Any] = {
        "schema": "pool-backpressure-smoke-v1",
        "status": "STARTING",
        "uart1": args.uart1,
        "uart3": args.uart3,
        "duration_seconds": args.duration_seconds,
        "heartbeat_seconds": args.heartbeat_seconds,
        "trigger_offsets": trigger_offsets,
        "started_at_utc": utc_now(),
    }
    write_json(output_dir / "run-state.json", state)

    finsh: FinshSession | None = None
    client: TerpClient | None = None
    transport: RecordingTransport | None = None
    started_at: str | None = None
    error: str | None = None
    heartbeats: list[dict[str, Any]] = []
    trigger_records: list[dict[str, Any]] = []
    event_readbacks: list[dict[str, Any]] = []
    seen_event_ids: set[int] = set()
    new_event_ids: set[int] = set()
    counter_max: dict[str, int] = {}
    pool_min_free_min: int | None = None
    health_records: list[dict[str, Any]] = []
    completed_duration = False

    def record_uart1_metrics(record: dict[str, Any]) -> None:
        nonlocal pool_min_free_min
        for key, value in record.get("metrics", {}).items():
            if key == "pool_min_free":
                pool_min_free_min = value if pool_min_free_min is None else min(pool_min_free_min, value)
            else:
                counter_max[key] = max(counter_max.get(key, 0), value)

    def terp_snapshot(
        index: int, elapsed: float, capture_new: bool = True
    ) -> dict[str, Any]:
        health = client.get_health()  # type: ignore[union-attr]
        model = client.model_ota_query()  # type: ignore[union-attr]
        events = list_all_events(client)  # type: ignore[arg-type]
        current_ids = {event.event_id for event in events}
        newly_seen_ids = current_ids - seen_event_ids
        if capture_new:
            for event_id in sorted(newly_seen_ids):
                readback = capture_event(client, event_id, event_dir)  # type: ignore[arg-type]
                event_readbacks.append(readback)
                new_event_ids.add(event_id)
        seen_event_ids.update(current_ids)
        record = {
            "heartbeat_index": index,
            "elapsed_seconds": round(elapsed, 3),
            "timestamp_utc": utc_now(),
            "health": asdict(health),
            "model_ota_query": asdict(model),
            "event_count": len(events),
            "event_ids": [event.event_id for event in events],
            "new_event_ids": sorted(newly_seen_ids) if capture_new else [],
            "health_ok": (
                health.storage_ready
                and health.storage_error_count == 0
                and health.event_export_error_count == 0
            ),
            "model_ok": model.model_valid and not model.pending_install,
        }
        health_records.append(record)
        return record

    try:
        finsh = FinshSession(args.uart1, uart1_dir)
        baseline_records = [
            finsh.command(command, 2.0)
            for command in ("sysinfo", "ps", "event status", "log status", "log inspect")
        ]
        for record in baseline_records:
            record_uart1_metrics(record)
        if any(not record["prompt_seen"] for record in baseline_records):
            raise RuntimeError("UART1 baseline command did not return msh prompt")

        def transport_factory() -> RecordingTransport:
            nonlocal transport
            transport = RecordingTransport(
                SerialTransport.open(args.uart3, baudrate=115200, timeout_seconds=0.2),
                uart3_dir / "runtime.raw.jsonl",
            )
            return transport

        client, device_info = TerpClient.connect_with_retry(
            transport_factory,
            startup_timeout_seconds=10.0,
            retry_interval_seconds=0.5,
            timeout_seconds=5.0,
        )
        baseline_terp = terp_snapshot(0, 0.0, capture_new=False)
        baseline_terp["device_info"] = asdict(device_info)
        expected_identity = {
            "model": "STM32H743",
            "firmware_version": "phase08-terp-uart3",
            "hardware_version": "openmv4-h743-pd8-pd9",
            "serial_number": "recorder-001",
            "capability_flags": 235,
        }
        if baseline_terp["device_info"] != expected_identity:
            raise RuntimeError("UART3 identity mismatch")
        if not baseline_terp["health_ok"] or not baseline_terp["model_ok"]:
            raise RuntimeError("UART3 baseline health/model gate failed")

        started_at = utc_now()
        started_monotonic = time.monotonic()
        state.update({"status": "RUNNING", "t0_started_at_utc": started_at})
        write_json(output_dir / "run-start.json", {"baseline_uart1": baseline_records, "baseline_uart3": baseline_terp})
        next_trigger = 0
        next_heartbeat = 0.0
        heartbeat_index = 1
        while True:
            elapsed = time.monotonic() - started_monotonic
            if elapsed >= args.duration_seconds:
                completed_duration = True
                break
            if next_trigger < len(trigger_offsets) and elapsed >= trigger_offsets[next_trigger]:
                trigger = finsh.command("event trigger_test", 2.5)
                record_uart1_metrics(trigger)
                trigger["trigger_index"] = next_trigger + 1
                trigger_records.append(trigger)
                write_json(output_dir / "triggers" / f"trigger-{next_trigger + 1:02d}.json", trigger)
                if not trigger["prompt_seen"]:
                    raise RuntimeError("event trigger_test did not return msh prompt")
                next_trigger += 1
            if elapsed >= next_heartbeat:
                uart1_records = [
                    finsh.command(command, 1.8) for command in ("ps", "event status")
                ]
                for record in uart1_records:
                    record_uart1_metrics(record)
                    if not record["prompt_seen"]:
                        raise RuntimeError(f"UART1 heartbeat timed out: {record['command']}")
                terp_record = terp_snapshot(heartbeat_index, elapsed)
                heartbeat = {
                    "heartbeat_index": heartbeat_index,
                    "elapsed_seconds": round(elapsed, 3),
                    "uart1": uart1_records,
                    "uart3": terp_record,
                    "counter_max_so_far": dict(counter_max),
                    "pool_min_free_min_so_far": pool_min_free_min,
                }
                heartbeats.append(heartbeat)
                write_json(heartbeat_dir / f"heartbeat-{heartbeat_index:03d}.json", heartbeat)
                if not terp_record["health_ok"] or not terp_record["model_ok"]:
                    raise RuntimeError("UART3 health/model failure observed")
                heartbeat_index += 1
                next_heartbeat = elapsed + args.heartbeat_seconds
            time.sleep(0.2)
        time.sleep(12.0)
        final_uart1 = [
            finsh.command(command, 2.0)
            for command in ("ps", "event status", "log status", "log inspect")
        ]
        for record in final_uart1:
            record_uart1_metrics(record)
        final_terp = terp_snapshot(heartbeat_index, time.monotonic() - started_monotonic)
        write_json(output_dir / "final-uart1.json", final_uart1)
        write_json(output_dir / "final-uart3.json", final_terp)
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        if client is not None:
            client.close()
        elif transport is not None:
            transport.close()
        if finsh is not None:
            finsh.close()

    fatal_seen = any(record.get("fatal_marker_seen") for record in (finsh.records if finsh else []))
    critical_counters = {key: value for key, value in counter_max.items() if value > 0}
    pass_candidate = bool(
        error is None
        and completed_duration
        and len(trigger_records) == len(trigger_offsets)
        and len(new_event_ids) >= len(trigger_offsets)
        and len(event_readbacks) >= len(trigger_offsets)
        and all(item.get("pass") for item in event_readbacks)
        and not fatal_seen
        and not critical_counters
        and pool_min_free_min is not None
        and pool_min_free_min > 0
        and health_records
        and all(item["health_ok"] and item["model_ok"] for item in health_records)
    )
    state.update(
        {
            "status": "SCOPED_PASS" if pass_candidate else "FAIL",
            "ended_at_utc": utc_now(),
            "trigger_count": len(trigger_records),
            "new_event_ids": sorted(new_event_ids),
            "event_readback_count": len(event_readbacks),
            "heartbeat_count": len(heartbeats),
            "counter_max": counter_max,
            "critical_counters": critical_counters,
            "pool_min_free_min": pool_min_free_min,
            "fatal_marker_seen": fatal_seen,
            "error": error,
            "pass_candidate": pass_candidate,
            "scope": "short event-export regression; not the deferred two-hour G3 gate",
        }
    )
    write_json(output_dir / "run-summary.json", state)
    if finsh is not None:
        state["uart1_raw"] = digest(finsh.raw_path)
        state["uart1_console"] = digest(finsh.console_path)
    uart3_raw = uart3_dir / "runtime.raw.jsonl"
    if uart3_raw.exists():
        state["uart3_raw"] = digest(uart3_raw)
    write_json(output_dir / "run-summary.json", state)
    print(json.dumps(state, ensure_ascii=False, indent=2))
    return 0 if pass_candidate else 1


if __name__ == "__main__":
    raise SystemExit(main())
