"""Command-line interface for the TERP device protocol."""

from __future__ import annotations

import argparse
from dataclasses import asdict, is_dataclass
import hashlib
import json
from pathlib import Path
import sys
from typing import Any, Sequence
import zlib

from .protocol.client import (
    ConnectionState,
    DeviceError,
    FrameSummary,
    ProtocolIncompatibleError,
    ProtocolError,
    TerpClient,
    TransportError,
)
from .protocol.messages import ErrorCode, MessageType
from .protocol.serial_transport import SerialTransport, list_serial_ports


EXIT_OK = 0
EXIT_INTERNAL = 1
EXIT_TRANSPORT = 2
EXIT_DEVICE_REJECTED = 3
EXIT_PROTOCOL = 4
EXIT_INCOMPATIBLE = 5


def _output_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--json", action="store_true", help="emit stable JSON")
    parser.add_argument("--verbose", action="store_true", help="print frame summaries")


def _port_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--port", required=True, help="Windows COM port, for example COM7")
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=15.0,
        help="seconds to wait for TERP readiness after reset",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="transport-recorder")
    commands = parser.add_subparsers(dest="command", required=True)

    ports = commands.add_parser("ports", help="list candidate serial ports")
    _output_arguments(ports)

    for name, help_text in (("info", "show device identity"),
                            ("health", "show health snapshot")):
        command = commands.add_parser(name, help=help_text)
        _port_argument(command)
        _output_arguments(command)

    time_command = commands.add_parser("time", help="get or set UTC time")
    _port_argument(time_command)
    _output_arguments(time_command)
    time_commands = time_command.add_subparsers(dest="time_command", required=True)
    time_commands.add_parser("get")
    set_time = time_commands.add_parser("set")
    set_time.add_argument("utc_unix_seconds", type=int)

    events = commands.add_parser("events", help="inspect and download saved events")
    event_commands = events.add_subparsers(dest="event_command", required=True)
    event_list = event_commands.add_parser("list")
    _port_argument(event_list)
    _output_arguments(event_list)
    event_list.add_argument("--after", type=int, default=0)
    event_list.add_argument("--limit", type=int, default=16)
    download = event_commands.add_parser("download")
    _port_argument(download)
    _output_arguments(download)
    download.add_argument("--id", type=int, required=True)
    download.add_argument("--output", type=Path, required=True)
    evidence = event_commands.add_parser(
        "evidence", help="download one event's reliability evidence"
    )
    _port_argument(evidence)
    _output_arguments(evidence)
    evidence.add_argument("--id", type=int, required=True)
    delete = event_commands.add_parser("delete")
    _port_argument(delete)
    _output_arguments(delete)
    delete.add_argument("--id", type=int, required=True)

    crash = commands.add_parser(
        "crash-record", help="read or acknowledge a retained CrashRecord"
    )
    crash_commands = crash.add_subparsers(dest="crash_command", required=True)
    crash_download = crash_commands.add_parser("download")
    _port_argument(crash_download)
    _output_arguments(crash_download)
    crash_download.add_argument("--sequence", type=int, required=True)
    crash_download.add_argument("--output", type=Path, required=True)
    crash_ack = crash_commands.add_parser("ack")
    _port_argument(crash_ack)
    _output_arguments(crash_ack)
    crash_ack.add_argument("--sequence", type=int, required=True)

    return parser


def _emit(value: Any, as_json: bool) -> None:
    if is_dataclass(value):
        value = asdict(value)
    elif isinstance(value, list):
        value = [asdict(item) if is_dataclass(item) else item for item in value]
    if as_json:
        print(json.dumps(value, sort_keys=True, separators=(",", ":")))
    elif isinstance(value, dict):
        for key, item in value.items():
            print(f"{key}: {item}")
    else:
        print(value)


def _message_type_name(message_type: int) -> str:
    try:
        return MessageType(message_type).name
    except ValueError:
        if (message_type & 0x8000) != 0:
            try:
                return f"{MessageType(message_type & 0x7FFF).name}_RESPONSE"
            except ValueError:
                pass
    return "UNKNOWN"


def _print_frame_summary(frame: FrameSummary) -> None:
    message_type = frame.message_type
    summary = (
        f"{frame.direction} "
        f"type={_message_type_name(message_type)}(0x{message_type:04X}) "
        f"sequence={frame.sequence} flags=0x{frame.flags:04X} "
        f"payload_bytes={frame.payload_length}"
    )
    print(summary, file=sys.stderr)


def _file_crc32(path: Path) -> int:
    checksum = 0
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            checksum = zlib.crc32(chunk, checksum)
    return checksum & 0xFFFFFFFF


def _open_client(
    port: str,
    verbose: bool,
    startup_timeout_seconds: float = 15.0,
) -> TerpClient:
    client, info = TerpClient.connect_with_retry(
        lambda: SerialTransport.open(port),
        startup_timeout_seconds=startup_timeout_seconds,
        trace=_print_frame_summary if verbose else None,
    )
    if verbose:
        print(f"connected port={port} serial={info.serial_number}", file=sys.stderr)
    return client


def run(args: argparse.Namespace) -> Any:
    if args.command == "ports":
        return list_serial_ports()

    client = _open_client(args.port, args.verbose, args.startup_timeout)
    try:
        if args.command == "info":
            return client.get_device_info()
        if args.command == "health":
            return client.get_health()
        if args.command == "time":
            return (client.get_time() if args.time_command == "get"
                    else client.set_time(args.utc_unix_seconds))
        if args.command == "events" and args.event_command == "list":
            events, next_event_id = client.list_events(args.after, args.limit)
            return {"events": [asdict(event) for event in events],
                    "next_event_id": next_event_id}
        if args.command == "events" and args.event_command == "download":
            event_info = client.get_event_info(args.id)
            output = client.download_event(
                args.id,
                args.output,
                reconnect=lambda: SerialTransport.open(args.port),
            )
            return {
                "event_id": args.id,
                "output": str(output),
                "device_event_info": asdict(event_info),
                "downloaded_crc32": _file_crc32(output),
            }
        if args.command == "events" and args.event_command == "evidence":
            return client.get_event_evidence(args.id)
        if args.command == "crash-record" and args.crash_command == "download":
            output = args.output
            output.parent.mkdir(parents=True, exist_ok=True)
            record = client.download_crash_record(sequence=args.sequence)
            if len(record) != 128:
                raise ProtocolError(
                    "CrashRecord download is not exactly 128 bytes"
                )
            output.write_bytes(record)
            return {
                "sequence": args.sequence,
                "output": str(output),
                "bytes": len(record),
                "sha256": hashlib.sha256(record).hexdigest().upper(),
            }
        if args.command == "crash-record" and args.crash_command == "ack":
            return {
                "sequence": client.ack_crash_record(args.sequence),
                "acknowledged": True,
            }
        if args.command == "events" and args.event_command == "delete":
            client.delete_event(args.id)
            return {"event_id": args.id, "deleted": True}
        raise AssertionError("unhandled CLI command")
    finally:
        client.close()


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        result = run(args)
        _emit(result, args.json)
        return EXIT_OK
    except ProtocolIncompatibleError as error:
        print(str(error), file=sys.stderr)
        return EXIT_INCOMPATIBLE
    except DeviceError as error:
        if error.code == ErrorCode.INCOMPATIBLE:
            print(f"protocol incompatible: {error}", file=sys.stderr)
            return EXIT_INCOMPATIBLE
        print(str(error), file=sys.stderr)
        return EXIT_DEVICE_REJECTED
    except TransportError as error:
        print(str(error), file=sys.stderr)
        return EXIT_TRANSPORT
    except ProtocolError as error:
        print(str(error), file=sys.stderr)
        return EXIT_PROTOCOL
    except Exception as error:
        print(str(error), file=sys.stderr)
        return EXIT_INTERNAL


if __name__ == "__main__":
    raise SystemExit(main())
