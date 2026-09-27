from __future__ import annotations

from pathlib import Path
import threading

import pytest

from host.transport_recorder.device.session import DeviceSession, SessionState
from host.transport_recorder.protocol.client import (
    AiResult,
    DownloadCancelled,
    DownloadProgress,
    TransportError,
)
from host.transport_recorder.protocol.codec import ProtocolError
from host.transport_recorder.protocol.simulated_device import MemoryTransport, SimulatedDevice
from host.transport_recorder.repository.event_repository import EventRepository, ValidationState


FIXTURE_EVENTS = Path(__file__).parents[1] / "fixtures" / "events"


def test_session_connects_lists_a_page_and_downloads_into_repository(tmp_path: Path) -> None:
    event_bytes = (FIXTURE_EVENTS / "valid-42.terp-event").read_bytes()
    device = SimulatedDevice({42: event_bytes})
    session = DeviceSession(lambda: MemoryTransport(device))
    repository = EventRepository(tmp_path)
    progress: list[DownloadProgress] = []

    connected = session.connect()
    page = session.list_events(after_event_id=0, limit=16)
    stored = session.download_to_repository(42, repository, progress=progress.append)

    assert connected.identity.serial_number == "SIM-0001"
    assert connected.health.storage_ready
    assert [item.event_id for item in page.events] == [42]
    assert stored.validation_state is ValidationState.VALID
    assert session.state is SessionState.READY
    assert progress[-1].verified_bytes == len(event_bytes)
    assert not (repository.downloads_root / "SIM-0001" / "42.terp-event").exists()


def test_session_reads_model_result_as_a_device_record_not_an_annotation() -> None:
    device = SimulatedDevice({})
    session = DeviceSession(lambda: MemoryTransport(device))
    expected = AiResult(
        event_id=42,
        model_version=2,
        status=1,
        class_index=1,
        class_count=2,
        quality_flags=3,
        event_flags=0,
        sample_count=2400,
        model_crc32=0xAABBCCDD,
        confidence=0.875,
        logits=(-1.0, 1.0, 0.0, 0.0),
        failure_reason=0,
        result_sequence=9,
    )

    session.connect()
    assert session._client is not None
    session._client.get_ai_result = lambda event_id: expected  # type: ignore[method-assign]

    assert session.get_ai_result(42) == expected


def test_session_connect_retries_until_terp_is_ready(tmp_path: Path) -> None:
    device = SimulatedDevice({})
    attempts = 0

    class NotReadyTransport:
        def write(self, data: bytes) -> None:
            del data
            raise TransportError("TERP startup is still in progress")

        def read(self, maximum_bytes: int) -> bytes:
            del maximum_bytes
            return b""

        def close(self) -> None:
            pass

    def transport_factory() -> object:
        nonlocal attempts
        attempts += 1
        return NotReadyTransport() if attempts < 3 else MemoryTransport(device)

    session = DeviceSession(transport_factory)
    connected = session.connect()

    assert connected.identity.serial_number == "SIM-0001"
    assert attempts == 3
    assert session.state is SessionState.READY


def test_cancelled_download_keeps_resumable_staging_files(tmp_path: Path) -> None:
    event_bytes = (FIXTURE_EVENTS / "valid-42.terp-event").read_bytes() * 100
    device = SimulatedDevice({42: event_bytes})
    session = DeviceSession(lambda: MemoryTransport(device))
    repository = EventRepository(tmp_path)
    checks = 0

    def cancelled() -> bool:
        nonlocal checks
        checks += 1
        return checks > 1

    session.connect()
    with pytest.raises(DownloadCancelled):
        session.download_to_repository(42, repository, cancelled=cancelled)

    staging = repository.downloads_root / "SIM-0001" / "42.terp-event"
    assert staging.with_name(staging.name + ".part").exists()
    assert session.state is SessionState.READY


def test_concurrent_device_operations_are_rejected_without_waiting(tmp_path: Path) -> None:
    event_bytes = (FIXTURE_EVENTS / "valid-42.terp-event").read_bytes()
    device = SimulatedDevice({42: event_bytes})
    session = DeviceSession(lambda: MemoryTransport(device))
    repository = EventRepository(tmp_path)
    entered = threading.Event()
    release = threading.Event()
    errors: list[BaseException] = []

    session.connect()
    assert session._client is not None

    def blocking_download(*args, **kwargs):
        entered.set()
        assert release.wait(timeout=2.0)
        raise DownloadCancelled("scripted cancellation")

    session._client.download_event = blocking_download  # type: ignore[method-assign]

    def run_download() -> None:
        try:
            session.download_to_repository(42, repository)
        except BaseException as error:
            errors.append(error)

    thread = threading.Thread(target=run_download)
    thread.start()
    assert entered.wait(timeout=1.0)
    with pytest.raises(RuntimeError, match="operation already in progress"):
        session.list_events(after_event_id=0, limit=16)
    with pytest.raises(RuntimeError, match="operation already in progress"):
        session.connect()
    release.set()
    thread.join(timeout=2.0)

    assert not thread.is_alive()
    assert len(errors) == 1 and isinstance(errors[0], DownloadCancelled)
    assert session.state is SessionState.READY


def test_protocol_failure_closes_client_and_does_not_report_ready(tmp_path: Path) -> None:
    event_bytes = (FIXTURE_EVENTS / "valid-42.terp-event").read_bytes()
    device = SimulatedDevice({42: event_bytes})
    session = DeviceSession(lambda: MemoryTransport(device))
    repository = EventRepository(tmp_path)

    session.connect()
    assert session._client is not None

    def fail_download(*args, **kwargs):
        raise ProtocolError("scripted CRC failure")

    session._client.download_event = fail_download  # type: ignore[method-assign]
    with pytest.raises(ProtocolError, match="scripted CRC failure"):
        session.download_to_repository(42, repository)

    assert session.state is SessionState.ERROR
    assert session.connected_device is None
    assert session._client is None
