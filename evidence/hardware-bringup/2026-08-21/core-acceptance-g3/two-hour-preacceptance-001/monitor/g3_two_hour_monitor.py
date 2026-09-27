#!/usr/bin/env python3
"""Run the bounded G3 two-hour concurrent hardware preacceptance monitor.

The monitor keeps the board powered and exercises the existing acquisition,
event, storage, AI and TERP paths.  It never formats storage, erases QSPI,
resets the board, or controls board power.  UART1 is used for read-only
metrics plus two safe test triggers; UART3 performs health/model/event
readback and four small A/B model activations.
"""

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

from transport_recorder.protocol.client import FrameSummary, TerpClient  # noqa: E402
from transport_recorder.protocol.serial_transport import SerialTransport  # noqa: E402


EXPECTED_IDENTITY = {
    "model": "STM32H743",
    "firmware_version": "phase08-terp-uart3",
    "hardware_version": "openmv4-h743-pd8-pd9",
    "serial_number": "recorder-001",
    "capability_flags": 235,
}

FATAL_MARKERS = ("HardFault", "assertion", "stack overflow", "watchdog reset")
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
# The G3 contract requires zero sample-pool backpressure.  Keep it in the
# immediate-stop set so a future run cannot report a misleading pass candidate.
CRITICAL_COUNTER_KEYS = set(COUNTER_KEYS)
COUNTER_PATTERN = re.compile(r"\b([A-Za-z0-9_]+)=(\d+)\b")


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


def error_text(error: BaseException) -> str:
    return f"{type(error).__name__}: {error}"


def extract_metrics(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for key, value in COUNTER_PATTERN.findall(text):
        if key in COUNTER_KEYS:
            values[key] = max(values.get(key, 0), int(value))
    return values


def critical_findings(record: dict[str, Any]) -> list[str]:
    findings: list[str] = []
    text = str(record.get("text", ""))
    for marker in FATAL_MARKERS:
        if marker.lower() in text.lower():
            findings.append(f"uart1:{marker}")
    for key, value in record.get("metrics", {}).items():
        if key in CRITICAL_COUNTER_KEYS and value > 0:
            findings.append(f"uart1:{key}={value}")
    return findings


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


class RecordingTransport:
    """Record TERP raw frames while preserving the ByteTransport interface."""

    def __init__(self, inner: SerialTransport, raw_path: Path) -> None:
        self._inner = inner
        raw_path.parent.mkdir(parents=True, exist_ok=True)
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


class FinshSession:
    """Bounded UART1 command/response capture without toggling control lines."""

    def __init__(self, port_name: str, output_dir: Path) -> None:
        self.port_name = port_name
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

    def _record(self, direction: str, data: bytes) -> None:
        if not data:
            return
        self._raw.write(data)
        self._raw.flush()
        text = data.decode("utf-8", errors="replace")
        self._console.write(f"[{utc_now()}] {direction}\n{text}\n")
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

    def command(self, command: str, wait_seconds: float = 1.5) -> dict[str, Any]:
        preamble = self._read_for(0.2)
        self._record("RX_PRE_COMMAND", preamble)
        payload = (command + "\r\n").encode("ascii")
        self._port.write(payload)
        self._record("TX", payload)
        response = self._read_for(wait_seconds)
        self._record("RX", response)
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
        record["critical_findings"] = critical_findings(record)
        return record

    def close(self) -> None:
        try:
            if self._port.is_open:
                self._port.close()
        finally:
            self._raw.close()
            self._console.close()


def capture_event(
    client: TerpClient,
    event_id: int,
    output_dir: Path,
    label: str,
) -> dict[str, Any]:
    entry: dict[str, Any] = {
        "event_id": event_id,
        "label": label,
        "started_at_utc": utc_now(),
    }
    try:
        info = client.get_event_info(event_id)
        output_path = output_dir / f"{label}-event-{event_id}.terp-event"
        client.download_event(event_id, output_path)
        entry["event_info"] = asdict(info)
        entry["download"] = digest(output_path)
        entry["ai_result"] = asdict(client.get_ai_result(event_id))
        entry["pass"] = True
    except Exception as error:  # retain the exact readback failure in evidence
        entry["error"] = error_text(error)
        entry["pass"] = False
    entry["ended_at_utc"] = utc_now()
    return entry


def terp_snapshot(
    client: TerpClient,
    trace_event_id: int | None,
    seen_event_ids: set[int],
    event_output_dir: Path,
    heartbeat_index: int,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    health = client.get_health()
    model = client.model_ota_query()
    events = list_all_events(client)
    current_ids = {event.event_id for event in events}
    new_events = [event for event in events if event.event_id not in seen_event_ids]
    event_readbacks: list[dict[str, Any]] = []
    for event in new_events:
        event_readbacks.append(
            capture_event(
                client,
                event.event_id,
                event_output_dir,
                f"heartbeat-{heartbeat_index:03d}",
            )
        )
    seen_event_ids.update(current_ids)

    result: dict[str, Any] = {
        "timestamp_utc": utc_now(),
        "health": asdict(health),
        "model_ota_query": asdict(model),
        "event_count": len(events),
        "event_ids": [event.event_id for event in events],
        "trace_event_id": trace_event_id,
        "new_event_readbacks": event_readbacks,
        "health_ok": (
            health.storage_ready
            and health.storage_error_count == 0
            and health.event_export_error_count == 0
        ),
        "model_ok": model.model_valid and not model.pending_install,
    }
    if trace_event_id is not None:
        result["trace_ai_result"] = asdict(client.get_ai_result(trace_event_id))
    return result, event_readbacks


def activate_model(
    client: TerpClient,
    label: str,
    package_path: Path,
    output_dir: Path,
) -> dict[str, Any]:
    package = package_path.read_bytes()
    entry: dict[str, Any] = {
        "label": label,
        "package_path": str(package_path),
        "package": {
            "bytes": len(package),
            "sha256": hashlib.sha256(package).hexdigest().upper(),
        },
        "started_at_utc": utc_now(),
    }
    try:
        progress = client.upload_model_package(package, chunk_bytes=256)
        query = client.model_ota_query()
        entry["finalize"] = asdict(progress)
        entry["query_after_finalize"] = asdict(query)
        entry["pass"] = (
            progress.verified_bytes == len(package)
            and not progress.pending_install
            and progress.model_valid
            and query.verified_bytes == len(package)
            and not query.pending_install
            and query.model_valid
        )
        if not entry["pass"]:
            entry["error"] = "model activation did not return a valid non-pending state"
    except Exception as error:
        entry["error"] = error_text(error)
        entry["pass"] = False
    entry["ended_at_utc"] = utc_now()
    write_json(output_dir / f"{label}-{entry['started_at_utc'].replace(':', '').replace('+00:00', 'Z')}.json", entry)
    return entry


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uart1", required=True, help="FinSH UART1 COM port")
    parser.add_argument("--uart3", required=True, help="TERP UART3 COM port")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--model-a", required=True, type=Path)
    parser.add_argument("--model-b", required=True, type=Path)
    parser.add_argument("--duration-seconds", type=float, default=7200.0)
    parser.add_argument("--heartbeat-seconds", type=float, default=60.0)
    parser.add_argument("--initial-seconds", type=float, default=600.0)
    parser.add_argument("--initial-heartbeat-seconds", type=float, default=30.0)
    parser.add_argument("--detailed-interval-seconds", type=float, default=300.0)
    return parser.parse_args()


def run() -> int:
    args = parse_args()
    if (
        args.duration_seconds <= 0
        or args.heartbeat_seconds <= 0
        or args.initial_seconds <= 0
        or args.initial_heartbeat_seconds <= 0
        or args.detailed_interval_seconds <= 0
    ):
        raise SystemExit("duration and all monitor intervals must be positive")

    output_dir = args.output_dir
    uart1_dir = output_dir / "uart1"
    uart3_dir = output_dir / "uart3"
    event_dir = output_dir / "event-readback"
    model_dir = output_dir / "model-switches"
    heartbeats_dir = output_dir / "heartbeats"
    for path in (uart1_dir, uart3_dir, event_dir, model_dir, heartbeats_dir):
        path.mkdir(parents=True, exist_ok=True)

    state: dict[str, Any] = {
        "schema": "g3-two-hour-monitor-state-20260821-v1",
        "status": "STARTING",
        "uart1": args.uart1,
        "uart3": args.uart3,
        "duration_seconds": args.duration_seconds,
        "heartbeat_seconds": args.heartbeat_seconds,
        "initial_seconds": args.initial_seconds,
        "initial_heartbeat_seconds": args.initial_heartbeat_seconds,
        "detailed_interval_seconds": args.detailed_interval_seconds,
        "started_at_utc": utc_now(),
        "t0_started_at_utc": None,
        "heartbeat_count": 0,
        "model_switch_count": 0,
        "event_trigger_count": 0,
        "anomalies": [],
    }
    write_json(output_dir / "run-state.json", state)

    finsh: FinshSession | None = None
    client: TerpClient | None = None
    frame_file = (uart3_dir / "runtime.frames.jsonl").open(
        "a", encoding="utf-8", newline="\n"
    )
    seen_event_ids: set[int] = set()
    trace_event_id: int | None = None
    anomalies: list[str] = []
    heartbeat_count = 0
    model_switch_count = 0
    event_trigger_count = 0
    success = False
    t0_started_at: str | None = None
    t0_monotonic: float | None = None
    stop_reason: str | None = None

    def record_anomalies(items: list[str]) -> None:
        for item in items:
            if item not in anomalies:
                anomalies.append(item)

    try:
        finsh = FinshSession(args.uart1, uart1_dir)
        baseline_commands = ("sysinfo", "ps", "event status", "log status", "log inspect")
        baseline_finsh = [finsh.command(command, 1.8) for command in baseline_commands]
        finsh_findings = [
            finding for record in baseline_finsh for finding in record["critical_findings"]
        ]
        if any(not record["prompt_seen"] for record in baseline_finsh):
            raise RuntimeError("UART1 baseline command did not return msh prompt")
        if finsh_findings:
            raise RuntimeError("UART1 baseline critical finding: " + ", ".join(finsh_findings))

        raw_path = uart3_dir / "runtime.raw.jsonl"

        def transport_factory() -> RecordingTransport:
            return RecordingTransport(
                SerialTransport.open(args.uart3, baudrate=115200, timeout_seconds=0.2),
                raw_path,
            )

        client, device_info = TerpClient.connect_with_retry(
            transport_factory,
            startup_timeout_seconds=10.0,
            retry_interval_seconds=0.5,
            timeout_seconds=5.0,
        )

        def trace_frame(frame: FrameSummary) -> None:
            frame_file.write(
                json.dumps(
                    {"timestamp_utc": utc_now(), **asdict(frame)},
                    separators=(",", ":"),
                )
                + "\n"
            )
            frame_file.flush()

        client.trace = trace_frame
        health = client.get_health()
        events = list_all_events(client)
        model = client.model_ota_query()
        device_info_dict = asdict(device_info)
        baseline_terp = {
            "device_info": device_info_dict,
            "health": asdict(health),
            "model_ota_query": asdict(model),
            "event_count": len(events),
            "event_ids": [event.event_id for event in events],
            "identity_match": device_info_dict == EXPECTED_IDENTITY,
            "health_ok": (
                health.storage_ready
                and health.storage_error_count == 0
                and health.event_export_error_count == 0
            ),
            "model_ok": model.model_valid and not model.pending_install,
        }
        if not (
            baseline_terp["identity_match"]
            and baseline_terp["health_ok"]
            and baseline_terp["model_ok"]
            and events
        ):
            raise RuntimeError("UART3 baseline did not satisfy the T0 gate")

        seen_event_ids.update(event.event_id for event in events)
        for event in reversed(events):
            try:
                client.get_ai_result(event.event_id)
                trace_event_id = event.event_id
                break
            except Exception:
                continue
        if trace_event_id is None:
            raise RuntimeError("no existing event has a readable AI result")

        t0_started_at = utc_now()
        t0_monotonic = time.monotonic()
        state.update(
            {
                "status": "RUNNING",
                "t0_started_at_utc": t0_started_at,
                "baseline_uart1": baseline_finsh,
                "baseline_uart3": baseline_terp,
                "trace_event_id": trace_event_id,
                "safety_exclusions": [
                    "board_flash_diagnostic=1",
                    "log format --confirm",
                    "U2 erase",
                    "board power interruption",
                    "USB-TTL unplug/reconnect",
                ],
            }
        )
        write_json(output_dir / "run-start.json", {
            "schema": "g3-two-hour-monitor-start-20260821-v1",
            "started_at_utc": t0_started_at,
            "baseline_uart1": baseline_finsh,
            "baseline_uart3": baseline_terp,
            "trace_event_id": trace_event_id,
            "event_generation": {
                "planned_triggers": [1200, 4800],
                "command": "event trigger_test",
                "export_command": "automatic event service export; no duplicate event export command",
            },
            "model_schedule_seconds": [600, 2400, 4200, 6000],
            "sampling_schedule": {
                "initial_seconds": args.initial_seconds,
                "initial_heartbeat_seconds": args.initial_heartbeat_seconds,
                "steady_heartbeat_seconds": args.heartbeat_seconds,
                "detailed_uart1_interval_seconds": args.detailed_interval_seconds,
                "light_uart1_commands": ["ps", "event status"],
                "detailed_uart1_commands": [
                    "ps",
                    "event status",
                    "log status",
                    "log inspect",
                ],
            },
        })
        write_json(output_dir / "run-state.json", state)
        print(f"T0_STARTED {t0_started_at} trace_event={trace_event_id}", flush=True)

        initial_readback = capture_event(
            client, trace_event_id, event_dir, "t0-trace"
        )
        write_json(output_dir / "t0-event-readback.json", initial_readback)
        if not initial_readback["pass"]:
            raise RuntimeError("T0 event readback failed")

        model_schedule = (
            (600.0, "A", args.model_a),
            (2400.0, "B", args.model_b),
            (4200.0, "A", args.model_a),
            (6000.0, "B", args.model_b),
        )
        event_schedule = (1200.0, 4800.0)
        next_model = 0
        next_event = 0
        next_heartbeat = 0.0
        last_detailed_elapsed = -args.detailed_interval_seconds
        last_model_slot: int | None = None

        while True:
            assert t0_monotonic is not None
            elapsed = time.monotonic() - t0_monotonic
            if elapsed >= args.duration_seconds:
                success = True
                break

            if next_model < len(model_schedule) and elapsed >= model_schedule[next_model][0]:
                _, label, package_path = model_schedule[next_model]
                activation = activate_model(client, label, package_path, model_dir)
                model_switch_count += 1
                state["model_switch_count"] = model_switch_count
                write_json(
                    model_dir / f"activation-{model_switch_count:02d}.json", activation
                )
                if not activation["pass"]:
                    raise RuntimeError(f"Model {label} activation failed")
                slot = activation["query_after_finalize"]["active_slot"]
                if last_model_slot is not None and slot == last_model_slot:
                    record_anomalies([f"model_active_slot_did_not_flip:{label}:{slot}"])
                last_model_slot = slot
                next_model += 1

            if next_event < len(event_schedule) and elapsed >= event_schedule[next_event]:
                trigger = finsh.command("event trigger_test", 2.0)
                event_trigger_count += 1
                state["event_trigger_count"] = event_trigger_count
                write_json(
                    output_dir / "event-triggers" / f"trigger-{event_trigger_count:02d}.json",
                    trigger,
                )
                record_anomalies(trigger["critical_findings"])
                if not trigger["prompt_seen"]:
                    raise RuntimeError("event trigger_test did not return msh prompt")
                next_event += 1

            if elapsed >= next_heartbeat:
                detailed = (
                    elapsed < args.initial_seconds
                    or elapsed - last_detailed_elapsed >= args.detailed_interval_seconds
                )
                uart1_commands = (
                    ("ps", "event status", "log status", "log inspect")
                    if detailed
                    else ("ps", "event status")
                )
                uart1_records = [
                    finsh.command(command, 1.5) for command in uart1_commands
                ]
                if detailed:
                    last_detailed_elapsed = elapsed
                for record in uart1_records:
                    record_anomalies(record["critical_findings"])
                    if not record["prompt_seen"]:
                        raise RuntimeError(
                            f"UART1 heartbeat command timed out: {record['command']}"
                        )
                terp_record, new_readbacks = terp_snapshot(
                    client,
                    trace_event_id,
                    seen_event_ids,
                    event_dir,
                    heartbeat_count,
                )
                if not terp_record["health_ok"]:
                    raise RuntimeError("TERP health/storage error observed")
                if not terp_record["model_ok"]:
                    raise RuntimeError("TERP model became invalid or pending")
                for readback in new_readbacks:
                    if not readback["pass"]:
                        raise RuntimeError("new event readback failed")
                heartbeat = {
                    "schema": "g3-two-hour-heartbeat-20260821-v1",
                    "heartbeat_index": heartbeat_count,
                    "elapsed_seconds": round(elapsed, 3),
                    "timestamp_utc": utc_now(),
                    "mode": "detailed" if detailed else "light",
                    "uart1_commands": list(uart1_commands),
                    "uart1": uart1_records,
                    "uart3": terp_record,
                    "anomalies_so_far": list(anomalies),
                }
                write_json(
                    heartbeats_dir / f"heartbeat-{heartbeat_count:03d}.json",
                    heartbeat,
                )
                heartbeat_count += 1
                state["heartbeat_count"] = heartbeat_count
                state["anomalies"] = list(anomalies)
                state["elapsed_seconds"] = round(elapsed, 3)
                write_json(output_dir / "run-state.json", state)
                print(
                    f"HEARTBEAT {heartbeat_count:03d} elapsed={elapsed:.0f}s "
                    f"events={terp_record['event_count']} "
                    f"health_ok={terp_record['health_ok']} "
                    f"model_slot={terp_record['model_ota_query']['active_slot']} "
                    f"anomalies={len(anomalies)}",
                    flush=True,
                )
                if anomalies:
                    raise RuntimeError("critical runtime finding: " + ", ".join(anomalies))
                next_heartbeat = elapsed + (
                    args.initial_heartbeat_seconds
                    if elapsed < args.initial_seconds
                    else args.heartbeat_seconds
                )

            time.sleep(0.2)

    except KeyboardInterrupt:
        stop_reason = "operator_interrupt"
    except Exception as error:
        stop_reason = error_text(error)
        record_anomalies([stop_reason])
    finally:
        if client is not None:
            client.close()
        if finsh is not None:
            finsh.close()
        frame_file.close()
        state.update(
            {
                "status": "COMPLETED_PENDING_REVIEW" if success else "STOPPED",
                "ended_at_utc": utc_now(),
                "t0_started_at_utc": t0_started_at,
                "heartbeat_count": heartbeat_count,
                "model_switch_count": model_switch_count,
                "event_trigger_count": event_trigger_count,
                "anomalies": list(anomalies),
                "stop_reason": stop_reason,
            }
        )
        write_json(output_dir / "run-state.json", state)
        summary = {
            "schema": "g3-two-hour-monitor-summary-20260821-v1",
            "status": state["status"],
            "pass_candidate": bool(success and not anomalies),
            "duration_seconds_requested": args.duration_seconds,
            "t0_started_at_utc": t0_started_at,
            "ended_at_utc": state["ended_at_utc"],
            "heartbeat_count": heartbeat_count,
            "model_switch_count": model_switch_count,
            "event_trigger_count": event_trigger_count,
            "trace_event_id": trace_event_id,
            "anomalies": list(anomalies),
            "stop_reason": stop_reason,
            "logs": {
                "uart1_raw": digest(uart1_dir / "runtime.raw.bin")
                if (uart1_dir / "runtime.raw.bin").exists()
                else None,
                "uart1_console": digest(uart1_dir / "runtime.console.log")
                if (uart1_dir / "runtime.console.log").exists()
                else None,
                "uart3_raw": digest(uart3_dir / "runtime.raw.jsonl")
                if (uart3_dir / "runtime.raw.jsonl").exists()
                else None,
                "uart3_frames": digest(uart3_dir / "runtime.frames.jsonl")
                if (uart3_dir / "runtime.frames.jsonl").exists()
                else None,
            },
        }
        write_json(output_dir / "run-summary.json", summary)

    print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)
    return 0 if summary["pass_candidate"] else 1


if __name__ == "__main__":
    raise SystemExit(run())
