import json
from types import SimpleNamespace

import pytest

from host.transport_recorder import cli
from host.transport_recorder.protocol.client import (
    DeviceError,
    FrameSummary,
    ProtocolIncompatibleError,
    TransportError,
)
from host.transport_recorder.protocol.messages import ErrorCode, MessageType


def test_ports_command_emits_stable_json(monkeypatch, capsys) -> None:
    monkeypatch.setattr(
        cli,
        "list_serial_ports",
        lambda: [
            {"device": "COM7", "description": "Transport recorder", "serial_number": "ABC"}
        ],
    )

    exit_code = cli.main(["ports", "--json"])

    assert exit_code == 0
    assert json.loads(capsys.readouterr().out) == [
        {"description": "Transport recorder", "device": "COM7", "serial_number": "ABC"}
    ]


def test_event_commands_accept_the_documented_port_position() -> None:
    args = cli.build_parser().parse_args(["events", "list", "--port", "COM7"])

    assert args.port == "COM7"
    assert args.event_command == "list"


def test_port_commands_accept_a_startup_readiness_timeout() -> None:
    args = cli.build_parser().parse_args(
        ["info", "--port", "COM8", "--startup-timeout", "12"]
    )

    assert args.startup_timeout == 12


def test_open_client_closes_the_transport_when_hello_fails(monkeypatch) -> None:
    class FailingClient:
        instance: "FailingClient | None" = None

        def __init__(self, transport: object) -> None:
            self.transport = transport
            self.closed = False
            FailingClient.instance = self

        def hello(self) -> object:
            raise TransportError("no TERP device")

        @classmethod
        def connect_with_retry(cls, transport_factory, **kwargs):
            del kwargs
            client = cls(transport_factory())
            try:
                return client, client.hello()
            except Exception:
                client.close()
                raise

        def close(self) -> None:
            self.closed = True

    monkeypatch.setattr(cli, "TerpClient", FailingClient)
    monkeypatch.setattr(cli.SerialTransport, "open", lambda port: object())

    with pytest.raises(TransportError, match="no TERP device"):
        cli._open_client("COM7", verbose=False)

    assert FailingClient.instance is not None
    assert FailingClient.instance.closed


def test_unknown_cli_failure_is_not_reported_as_protocol_incompatibility(
    monkeypatch, capsys
) -> None:
    def fail_run(args: object) -> object:
        del args
        raise OSError("output directory is read-only")

    monkeypatch.setattr(cli, "run", fail_run)

    exit_code = cli.main(["ports"])

    assert exit_code == cli.EXIT_INTERNAL
    assert exit_code != cli.EXIT_INCOMPATIBLE
    assert "output directory is read-only" in capsys.readouterr().err


def test_device_version_rejection_uses_incompatible_exit_code(
    monkeypatch, capsys
) -> None:
    def fail_run(args: object) -> object:
        del args
        raise DeviceError(ErrorCode.INCOMPATIBLE, MessageType.HELLO)

    monkeypatch.setattr(cli, "run", fail_run)

    exit_code = cli.main(["ports"])

    assert exit_code == cli.EXIT_INCOMPATIBLE
    assert "incompatible" in capsys.readouterr().err.lower()


def test_successful_hello_with_unsupported_version_uses_incompatible_exit_code(
    monkeypatch, capsys
) -> None:
    def fail_run(args: object) -> object:
        del args
        raise ProtocolIncompatibleError("unsupported device TERP version 2")

    monkeypatch.setattr(cli, "run", fail_run)

    exit_code = cli.main(["ports"])

    assert exit_code == cli.EXIT_INCOMPATIBLE
    assert "unsupported device TERP version 2" in capsys.readouterr().err


def test_verbose_client_prints_frame_metadata_without_payload(
    monkeypatch, capsys
) -> None:
    class TracedClient:
        def __init__(self, transport: object) -> None:
            self.transport = transport
            self.trace = None

        def hello(self) -> object:
            assert self.trace is not None
            self.trace(
                FrameSummary("TX", MessageType.HELLO, 0, 7, 23)
            )
            self.trace(
                FrameSummary("RX", 0x8001, 0x8000, 7, 23)
            )
            return SimpleNamespace(serial_number="SIM-0001")

        @classmethod
        def connect_with_retry(cls, transport_factory, **kwargs):
            client = cls(transport_factory())
            client.trace = kwargs["trace"]
            return client, client.hello()

        def close(self) -> None:
            pass

    monkeypatch.setattr(cli, "TerpClient", TracedClient)
    monkeypatch.setattr(cli.SerialTransport, "open", lambda port: object())

    cli._open_client("COM7", verbose=True)

    stderr = capsys.readouterr().err
    assert "TX type=HELLO(0x0001) sequence=7 flags=0x0000 payload_bytes=23" in stderr
    assert (
        "RX type=HELLO_RESPONSE(0x8001) sequence=7 flags=0x8000 payload_bytes=23"
        in stderr
    )
