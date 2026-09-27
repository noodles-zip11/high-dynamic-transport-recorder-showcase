from pathlib import Path
import struct
import zlib

import pytest

from host.transport_recorder.protocol.client import (
    AiResult,
    ConnectionState,
    DeviceError,
    DownloadCancelled,
    DownloadProgress,
    ModelOtaProgress,
    ProtocolIncompatibleError,
    TerpClient,
    TransportError,
)
from host.transport_recorder.protocol.messages import ErrorCode, response_type
from host.transport_recorder.protocol.codec import Frame
from host.transport_recorder.protocol.messages_generated import MessageType
from host.transport_recorder.protocol.simulated_device import (
    DisconnectingTransportFactory,
    MemoryTransport,
    SimulatedDevice,
)


class AiResultDevice(SimulatedDevice):
    def handle(self, request: Frame) -> Frame:
        if request.message_type == MessageType.GET_AI_RESULT:
            if request.payload != struct.pack("<I", 42):
                return self._error(request, ErrorCode.NOT_FOUND)
            return self._response(
                request,
                struct.pack(
                    "<I H 4B H I I f 4f H H I",
                    42,
                    2,
                    1,
                    1,
                    2,
                    3,
                    8,
                    2400,
                    0xAABBCCDD,
                    0.875,
                    -1.0,
                    1.0,
                    0.0,
                    0.0,
                    0,
                    0,
                    9,
                ),
            )
        return super().handle(request)


class ModelOtaDevice(SimulatedDevice):
    def __init__(self) -> None:
        super().__init__({})
        self.package = b""
        self.expected_length = 0
        self.active_slot = 2
        self.model_valid = 0

    def _progress(self, request: Frame, *, pending: int = 0) -> Frame:
        return self._response(
            request,
            struct.pack(
                "<IIBBB",
                self.expected_length,
                len(self.package),
                pending,
                self.active_slot,
                self.model_valid,
            ),
        )

    def handle(self, request: Frame) -> Frame:
        if request.message_type == MessageType.MODEL_OTA_BEGIN:
            self.expected_length = struct.unpack("<I", request.payload)[0]
            self.package = b""
            self.active_slot = 2
            self.model_valid = 0
            return self._progress(request)
        if request.message_type == MessageType.MODEL_OTA_WRITE_CHUNK:
            offset, chunk_crc = struct.unpack_from("<II", request.payload)
            chunk = request.payload[8:]
            assert offset == len(self.package)
            assert chunk_crc == zlib.crc32(chunk) & 0xFFFFFFFF
            self.package += chunk
            return self._progress(request)
        if request.message_type == MessageType.MODEL_OTA_QUERY:
            return self._progress(request)
        if request.message_type == MessageType.MODEL_OTA_FINALIZE:
            assert len(self.package) == self.expected_length
            self.active_slot = 1
            self.model_valid = 1
            return self._progress(request)
        if request.message_type == MessageType.MODEL_OTA_CANCEL:
            self.package = b""
            self.expected_length = 0
            return self._response(request, b"")
        return super().handle(request)


def test_client_handshakes_and_downloads_without_deleting_the_event(
    tmp_path: Path,
) -> None:
    event_data = bytes(range(251)) * 20
    device = SimulatedDevice({42: event_data})
    client = TerpClient(MemoryTransport(device))

    info = client.hello()
    downloaded = client.download_event(42, tmp_path / "42.terp-event")

    assert client.state is ConnectionState.READY
    assert info.serial_number == "SIM-0001"
    assert downloaded.read_bytes() == event_data
    assert 42 in device.events


def test_client_reads_traceable_ai_result_without_confusing_it_with_annotation() -> None:
    client = TerpClient(MemoryTransport(AiResultDevice({42: b"event"})))

    client.hello()
    result = client.get_ai_result(42)

    assert isinstance(result, AiResult)
    assert result.event_id == 42
    assert result.model_version == 2
    assert result.status == 1
    assert result.class_index == 1
    assert result.class_count == 2
    assert result.quality_flags == 3
    assert result.event_flags == 8
    assert result.sample_count == 2400
    assert result.model_crc32 == 0xAABBCCDD
    assert result.confidence == pytest.approx(0.875)
    assert result.logits[:2] == pytest.approx((-1.0, 1.0))
    assert result.failure_reason == 0
    assert result.result_sequence == 9


def test_client_uploads_model_package_with_verified_progress() -> None:
    device = ModelOtaDevice()
    client = TerpClient(MemoryTransport(device))
    package = bytes(range(251)) * 3
    progress: list[ModelOtaProgress] = []

    client.hello()
    result = client.upload_model_package(package, chunk_bytes=128, progress=progress.append)

    assert result == ModelOtaProgress(
        total_bytes=len(package),
        verified_bytes=len(package),
        pending_install=False,
        active_slot=1,
        model_valid=True,
    )
    assert progress[-1] == result
    assert device.package == package


def test_connect_with_retry_reopens_until_terp_is_ready() -> None:
    device = SimulatedDevice({})
    attempts = 0
    closed_attempts = 0

    class NotReadyTransport:
        def write(self, data: bytes) -> None:
            del data
            raise TransportError("TERP startup is still in progress")

        def read(self, maximum_bytes: int) -> bytes:
            del maximum_bytes
            return b""

        def close(self) -> None:
            nonlocal closed_attempts
            closed_attempts += 1

    def transport_factory() -> object:
        nonlocal attempts
        attempts += 1
        return NotReadyTransport() if attempts < 3 else MemoryTransport(device)

    client, info = TerpClient.connect_with_retry(
        transport_factory,
        startup_timeout_seconds=1.0,
        retry_interval_seconds=0.0,
        timeout_seconds=0.01,
    )
    try:
        assert info.serial_number == "SIM-0001"
        assert client.state is ConnectionState.READY
    finally:
        client.close()

    assert attempts == 3
    assert closed_attempts == 2


def test_connect_with_retry_does_not_retry_protocol_incompatibility() -> None:
    attempts = 0

    class VersionTwoDevice:
        def handle(self, request: Frame) -> Frame:
            return Frame(
                response_type(request.message_type),
                0x8000,
                request.sequence,
                struct.pack("<BIII", 2, 4096, 4000, 0),
            )

    def transport_factory() -> object:
        nonlocal attempts
        attempts += 1
        return MemoryTransport(VersionTwoDevice())

    with pytest.raises(ProtocolIncompatibleError):
        TerpClient.connect_with_retry(
            transport_factory,
            startup_timeout_seconds=1.0,
            retry_interval_seconds=0.0,
            timeout_seconds=0.01,
        )

    assert attempts == 1


def test_connect_with_retry_reports_a_bounded_readiness_timeout() -> None:
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
        return NotReadyTransport()

    with pytest.raises(TransportError, match="TERP readiness timed out"):
        TerpClient.connect_with_retry(
            transport_factory,
            startup_timeout_seconds=0.001,
            retry_interval_seconds=0.0,
            timeout_seconds=0.01,
        )

    assert attempts >= 1


def test_client_trace_observes_summaries_without_changing_io() -> None:
    observed: list[tuple[object, ...]] = []
    client = TerpClient(MemoryTransport(SimulatedDevice({})))
    client.trace = lambda *arguments: observed.append(arguments)

    client.hello()

    assert all(len(arguments) == 1 for arguments in observed)
    summaries = [arguments[0] for arguments in observed]
    assert [summary.direction for summary in summaries] == ["TX", "RX", "TX", "RX"]
    assert [summary.message_type for summary in summaries] == [
        MessageType.HELLO,
        response_type(MessageType.HELLO),
        MessageType.GET_DEVICE_INFO,
        response_type(MessageType.GET_DEVICE_INFO),
    ]
    assert [summary.payload_length for summary in summaries] == [1, 13, 0, 47]
    assert all(not hasattr(summary, "payload") for summary in summaries)


def test_client_trace_failure_does_not_change_protocol_io_or_state() -> None:
    client = TerpClient(MemoryTransport(SimulatedDevice({})))

    def fail_trace(summary: object) -> None:
        del summary
        raise RuntimeError("diagnostic sink failed")

    client.trace = fail_trace

    info = client.hello()

    assert info.serial_number == "SIM-0001"
    assert client.state is ConnectionState.READY


def test_successful_hello_with_an_unsupported_version_is_incompatible() -> None:
    class VersionTwoDevice:
        def handle(self, request: Frame) -> Frame:
            return Frame(
                response_type(request.message_type),
                0x8000,
                request.sequence,
                struct.pack("<BIII", 2, 4096, 4000, 0),
            )

    client = TerpClient(MemoryTransport(VersionTwoDevice()))

    with pytest.raises(ProtocolIncompatibleError):
        client.hello()

    assert client.state is ConnectionState.INCOMPATIBLE


def test_device_rejects_event_operations_until_hello_completes() -> None:
    client = TerpClient(MemoryTransport(SimulatedDevice({42: b"event"})))

    with pytest.raises(DeviceError) as raised:
        client.get_event_info(42)

    assert raised.value.code is ErrorCode.HANDSHAKE_REQUIRED


def test_simulated_device_distinguishes_malformed_and_incompatible_hello() -> None:
    device = SimulatedDevice({})

    malformed = device.handle(Frame(MessageType.HELLO, 0, 1, b""))
    incompatible = device.handle(Frame(MessageType.HELLO, 0, 2, b"\x02"))

    assert malformed.payload[:2] == bytes([ErrorCode.MALFORMED, 0])
    assert incompatible.payload[:2] == bytes([ErrorCode.INCOMPATIBLE, 0])


def test_request_timeout_marks_the_client_as_error() -> None:
    class NoResponseTransport:
        def write(self, data: bytes) -> None:
            del data

        def read(self, maximum_bytes: int) -> bytes:
            del maximum_bytes
            return b""

        def close(self) -> None:
            pass

    client = TerpClient(NoResponseTransport(), timeout_seconds=0.001)

    with pytest.raises(TransportError, match="timed out"):
        client.get_device_info()

    assert client.state is ConnectionState.ERROR


def test_download_resumes_after_twenty_disconnects_and_verifies_the_full_crc(
    tmp_path: Path,
) -> None:
    event_data = bytes(range(256)) * (10 * 1024 * 1024 // 256)
    device = SimulatedDevice({7: event_data})
    factory = DisconnectingTransportFactory(device, disconnect_count=20)
    client = TerpClient(factory.create())

    client.hello()
    downloaded = client.download_event(
        7,
        tmp_path / "7.terp-event",
        reconnect=factory.create,
        chunk_bytes=4000,
    )

    assert factory.disconnects_seen == 20
    assert downloaded.read_bytes() == event_data
    assert zlib.crc32(downloaded.read_bytes()) & 0xFFFFFFFF == zlib.crc32(event_data) & 0xFFFFFFFF
    assert not (tmp_path / "7.terp-event.part").exists()


def test_download_cancellation_keeps_verified_partial_files(tmp_path: Path) -> None:
    event_data = bytes(range(256)) * 100
    client = TerpClient(MemoryTransport(SimulatedDevice({7: event_data})))
    cancellation_checks = 0

    def cancelled() -> bool:
        nonlocal cancellation_checks
        cancellation_checks += 1
        return cancellation_checks > 2

    client.hello()
    with pytest.raises(DownloadCancelled):
        client.download_event(
            7,
            tmp_path / "7.terp-event",
            chunk_bytes=4_000,
            cancelled=cancelled,
        )

    assert (tmp_path / "7.terp-event.part").exists()
    assert (tmp_path / "7.terp-event.part.json").exists()
    assert 0 < (tmp_path / "7.terp-event.part").stat().st_size < len(event_data)


def test_download_reports_only_verified_chunk_progress(tmp_path: Path) -> None:
    event_data = bytes(range(256)) * 40
    client = TerpClient(MemoryTransport(SimulatedDevice({7: event_data})))
    progress: list[DownloadProgress] = []

    client.hello()
    client.download_event(
        7,
        tmp_path / "7.terp-event",
        chunk_bytes=2_000,
        progress=progress.append,
    )

    assert progress
    assert progress[-1] == DownloadProgress(event_id=7, total_bytes=len(event_data), verified_bytes=len(event_data))
    assert all(0 < item.verified_bytes <= item.total_bytes for item in progress)
