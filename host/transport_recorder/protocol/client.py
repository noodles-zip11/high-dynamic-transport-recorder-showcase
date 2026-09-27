"""Stateful TERP client with verified, resumable event downloads."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum, IntEnum
import json
import os
from pathlib import Path
import struct
import time
from typing import Callable, Protocol
import zlib

from .codec import Frame, ProtocolError, crc32, encode_frame
from .messages import (
    TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1,
    ErrorCode,
    MessageType,
    response_type,
)
from .parser import StreamingParser

MODEL_OTA_MAX_PACKAGE_BYTES = 2528


class ByteTransport(Protocol):
    def write(self, data: bytes) -> None: ...

    def read(self, maximum_bytes: int) -> bytes: ...

    def close(self) -> None: ...


class TransportError(RuntimeError):
    """A transport fault that may be recoverable by reconnecting."""


class TransportDisconnected(TransportError):
    """The device or its byte transport disconnected."""


class DownloadCancelled(RuntimeError):
    """The host requested cancellation between verified TERP chunks."""


class ProtocolIncompatibleError(ProtocolError):
    """The peer completed HELLO with an unsupported TERP version."""


class CapabilityUnavailableError(ProtocolError):
    """The requested additive operation is not advertised by the device."""


class DeviceError(RuntimeError):
    def __init__(self, code: ErrorCode | int, request_type: int) -> None:
        self.code = ErrorCode(code) if code in ErrorCode._value2member_map_ else code
        self.request_type = request_type
        super().__init__(f"device rejected 0x{request_type:04X}: {self.code}")


class ConnectionState(Enum):
    DISCONNECTED = "disconnected"
    OPENING = "opening"
    HANDSHAKING = "handshaking"
    READY = "ready"
    TRANSFERRING = "transferring"
    INCOMPATIBLE = "incompatible"
    ERROR = "error"


@dataclass(frozen=True)
class DeviceInfo:
    model: str
    firmware_version: str
    hardware_version: str
    serial_number: str
    capability_flags: int


@dataclass(frozen=True)
class HealthInfo:
    state: int
    storage_ready: bool
    free_log_bytes: int
    storage_error_count: int
    event_export_error_count: int


@dataclass(frozen=True)
class TimeInfo:
    utc_unix_seconds: int
    epoch_id: int


@dataclass(frozen=True)
class EventInfo:
    event_id: int
    total_length: int
    event_crc32: int


class ReliabilityVerdict(IntEnum):
    PASS = 1
    DEGRADED = 2
    INVALID = 3


class ReliabilityAiDecision(IntEnum):
    NOT_RUN_QUALITY = 0
    ELIGIBLE_NO_RESULT = 1
    RESULT_PRESENT = 2


class ReliabilityStorageState(IntEnum):
    EL01_VERIFIED = 1


@dataclass(frozen=True)
class EventEvidence:
    event_id: int
    evidence_version: int
    verdict: ReliabilityVerdict
    ai_decision: ReliabilityAiDecision
    reason_flags: int
    storage_state: ReliabilityStorageState
    ai_result_status: int
    ai_failure_reason: int
    ai_result_sequence: int


@dataclass(frozen=True)
class CrashRecordChunk:
    sequence: int
    actual_offset: int
    total_length: int
    actual_length: int
    chunk_crc32: int
    data: bytes


@dataclass(frozen=True)
class AiResult:
    event_id: int
    model_version: int
    status: int
    class_index: int
    class_count: int
    quality_flags: int
    event_flags: int
    sample_count: int
    model_crc32: int
    confidence: float
    logits: tuple[float, ...]
    failure_reason: int
    result_sequence: int


@dataclass(frozen=True)
class DownloadProgress:
    event_id: int
    total_bytes: int
    verified_bytes: int


@dataclass(frozen=True)
class ModelOtaProgress:
    total_bytes: int
    verified_bytes: int
    pending_install: bool
    active_slot: int
    model_valid: bool


@dataclass(frozen=True)
class FrameSummary:
    direction: str
    message_type: int
    flags: int
    sequence: int
    payload_length: int


FrameTrace = Callable[[FrameSummary], None]


def _read_text(payload: bytes, offset: int) -> tuple[str, int]:
    if offset >= len(payload):
        raise ProtocolError("truncated TERP text field")
    length = payload[offset]
    end = offset + 1 + length
    if end > len(payload):
        raise ProtocolError("truncated TERP text value")
    return payload[offset + 1:end].decode("utf-8"), end


class TerpClient:
    def __init__(self, transport: ByteTransport, timeout_seconds: float = 2.0) -> None:
        self.transport = transport
        self.timeout_seconds = timeout_seconds
        self.trace: FrameTrace | None = None
        self.state = ConnectionState.OPENING
        self._parser = StreamingParser()
        self._next_sequence = 1
        self.maximum_payload_bytes = 0
        self.maximum_event_chunk_bytes = 0
        self.capability_flags = 0

    @classmethod
    def connect_with_retry(
        cls,
        transport_factory: Callable[[], ByteTransport],
        *,
        startup_timeout_seconds: float = 15.0,
        retry_interval_seconds: float = 0.5,
        timeout_seconds: float = 2.0,
        trace: FrameTrace | None = None,
    ) -> tuple["TerpClient", DeviceInfo]:
        """Open a TERP session, waiting for the device to finish booting.

        Only transport-level failures are retried. Protocol incompatibility,
        malformed frames, and device rejections are surfaced immediately so a
        bad connection cannot be hidden by the startup wait.
        """
        if startup_timeout_seconds <= 0.0:
            raise ValueError("startup_timeout_seconds must be positive")
        if retry_interval_seconds < 0.0:
            raise ValueError("retry_interval_seconds must not be negative")

        deadline = time.monotonic() + startup_timeout_seconds
        last_error: TransportError | None = None
        while True:
            client: TerpClient | None = None
            try:
                client = cls(
                    transport_factory(),
                    timeout_seconds=timeout_seconds,
                )
                client.trace = trace
                return client, client.hello()
            except TransportError as error:
                last_error = error
                if client is not None:
                    client.close()

                remaining = deadline - time.monotonic()
                if remaining <= 0.0:
                    raise TransportError(
                        "TERP readiness timed out after "
                        f"{startup_timeout_seconds:g}s: {last_error}"
                    ) from error
                if retry_interval_seconds > 0.0:
                    time.sleep(min(retry_interval_seconds, remaining))

    def _trace_frame(self, direction: str, frame: Frame) -> None:
        if self.trace is not None:
            try:
                self.trace(
                    FrameSummary(
                        direction=direction,
                        message_type=int(frame.message_type),
                        flags=frame.flags,
                        sequence=frame.sequence,
                        payload_length=len(frame.payload),
                    )
                )
            except Exception:
                self.trace = None

    def hello(self) -> DeviceInfo:
        self.state = ConnectionState.HANDSHAKING
        try:
            response = self._request(MessageType.HELLO, b"\x01")
            if len(response.payload) != 13:
                raise ProtocolError("invalid HELLO response length")
            version, payload_maximum, chunk_maximum, capabilities = struct.unpack(
                "<BIII", response.payload
            )
            if version != 1:
                self.state = ConnectionState.INCOMPATIBLE
                raise ProtocolIncompatibleError(
                    f"unsupported device TERP version {version}"
                )
            self.maximum_payload_bytes = payload_maximum
            self.maximum_event_chunk_bytes = chunk_maximum
            self.capability_flags = capabilities
            self.state = ConnectionState.READY
            return self.get_device_info()
        except Exception:
            if self.state is not ConnectionState.INCOMPATIBLE:
                self.state = ConnectionState.ERROR
            raise

    def get_device_info(self) -> DeviceInfo:
        payload = self._request(MessageType.GET_DEVICE_INFO, b"").payload
        model, offset = _read_text(payload, 0)
        firmware, offset = _read_text(payload, offset)
        hardware, offset = _read_text(payload, offset)
        serial_number, offset = _read_text(payload, offset)
        if len(payload) != offset + 4:
            raise ProtocolError("invalid GET_DEVICE_INFO response length")
        return DeviceInfo(model, firmware, hardware, serial_number,
                          struct.unpack_from("<I", payload, offset)[0])

    def get_health(self) -> HealthInfo:
        payload = self._request(MessageType.GET_HEALTH, b"").payload
        if len(payload) != 14:
            raise ProtocolError("invalid GET_HEALTH response length")
        state, storage_ready, free_bytes, storage_errors, export_errors = struct.unpack(
            "<BBIII", payload
        )
        return HealthInfo(state, bool(storage_ready), free_bytes, storage_errors,
                          export_errors)

    def get_time(self) -> TimeInfo:
        payload = self._request(MessageType.GET_TIME, b"").payload
        if len(payload) != 12:
            raise ProtocolError("invalid GET_TIME response length")
        return TimeInfo(*struct.unpack("<qI", payload))

    def set_time(self, utc_unix_seconds: int) -> TimeInfo:
        payload = self._request(MessageType.SET_TIME,
                                struct.pack("<q", utc_unix_seconds)).payload
        if len(payload) != 12:
            raise ProtocolError("invalid SET_TIME response length")
        return TimeInfo(*struct.unpack("<qI", payload))

    def list_events(self, after_event_id: int = 0, maximum_count: int = 16) -> tuple[list[EventInfo], int]:
        payload = self._request(
            MessageType.LIST_EVENTS, struct.pack("<II", after_event_id, maximum_count)
        ).payload
        if len(payload) < 6:
            raise ProtocolError("truncated LIST_EVENTS response")
        next_event_id, count = struct.unpack_from("<IH", payload)
        if len(payload) != 6 + count * 12:
            raise ProtocolError("invalid LIST_EVENTS response length")
        events = [
            EventInfo(*struct.unpack_from("<III", payload, 6 + index * 12))
            for index in range(count)
        ]
        return events, next_event_id

    def get_event_info(self, event_id: int) -> EventInfo:
        payload = self._request(MessageType.GET_EVENT_INFO,
                                struct.pack("<I", event_id)).payload
        if len(payload) != 12:
            raise ProtocolError("invalid GET_EVENT_INFO response length")
        return EventInfo(*struct.unpack("<III", payload))

    def get_ai_result(self, event_id: int = 0) -> AiResult:
        payload = self._request(
            MessageType.GET_AI_RESULT, struct.pack("<I", event_id)
        ).payload
        if len(payload) != 48:
            raise ProtocolError("invalid GET_AI_RESULT response length")
        (
            received_event_id,
            model_version,
            status,
            class_index,
            class_count,
            quality_flags,
            event_flags,
            sample_count,
            model_crc32,
            confidence,
            logit_0,
            logit_1,
            logit_2,
            logit_3,
            failure_reason,
            reserved,
            result_sequence,
        ) = struct.unpack("<I H 4B H I I f 4f H H I", payload)
        if reserved != 0 or received_event_id == 0:
            raise ProtocolError("invalid GET_AI_RESULT reserved or identity field")
        if event_id != 0 and received_event_id != event_id:
            raise ProtocolError("GET_AI_RESULT identity mismatch")
        return AiResult(
            event_id=received_event_id,
            model_version=model_version,
            status=status,
            class_index=class_index,
            class_count=class_count,
            quality_flags=quality_flags,
            event_flags=event_flags,
            sample_count=sample_count,
            model_crc32=model_crc32,
            confidence=confidence,
            logits=(logit_0, logit_1, logit_2, logit_3),
            failure_reason=failure_reason,
            result_sequence=result_sequence,
        )

    def get_event_evidence(self, event_id: int) -> EventEvidence:
        self._require_reliability_capability()
        if not 0 < event_id <= 0xFFFFFFFF:
            raise ValueError("event ID must be a nonzero uint32")
        payload = self._request(
            MessageType.GET_EVENT_EVIDENCE,
            struct.pack("<I", event_id),
        ).payload
        if len(payload) != 20:
            raise ProtocolError("invalid GET_EVENT_EVIDENCE response length")
        (
            received_event_id,
            evidence_version,
            verdict,
            ai_decision,
            reason_flags,
            storage_state,
            ai_result_status,
            ai_failure_reason,
            ai_result_sequence,
        ) = struct.unpack("<I H B B I B B H I", payload)
        if received_event_id != event_id or evidence_version != 1:
            raise ProtocolError(
                "GET_EVENT_EVIDENCE identity or version mismatch"
            )
        try:
            verdict_value = ReliabilityVerdict(verdict)
            decision_value = ReliabilityAiDecision(ai_decision)
            storage_value = ReliabilityStorageState(storage_state)
        except ValueError as error:
            raise ProtocolError(
                "invalid GET_EVENT_EVIDENCE enum value"
            ) from error
        if decision_value is not ReliabilityAiDecision.RESULT_PRESENT:
            if (
                ai_result_status != 0
                or ai_failure_reason != 0
                or ai_result_sequence != 0
            ):
                raise ProtocolError("unexpected absent AI result fields")
        if verdict_value is not ReliabilityVerdict.PASS:
            if decision_value is not ReliabilityAiDecision.NOT_RUN_QUALITY:
                raise ProtocolError("non-PASS evidence carries an AI decision")
        elif decision_value is ReliabilityAiDecision.NOT_RUN_QUALITY:
            raise ProtocolError("PASS evidence has no AI decision")
        return EventEvidence(
            event_id=received_event_id,
            evidence_version=evidence_version,
            verdict=verdict_value,
            ai_decision=decision_value,
            reason_flags=reason_flags,
            storage_state=storage_value,
            ai_result_status=ai_result_status,
            ai_failure_reason=ai_failure_reason,
            ai_result_sequence=ai_result_sequence,
        )

    def get_crash_record_chunk(
        self,
        sequence: int,
        offset: int,
        requested_length: int,
    ) -> CrashRecordChunk:
        self._require_reliability_capability()
        if not 0 <= sequence <= 0xFFFFFFFF:
            raise ValueError("CrashRecord sequence must be a uint32")
        if not 0 <= offset < 128:
            raise ValueError("CrashRecord offset must be between 0 and 127")
        if not 0 < requested_length <= 128 - offset:
            raise ValueError("CrashRecord length exceeds the 128-byte record")
        payload = self._request(
            MessageType.GET_CRASH_RECORD,
            struct.pack("<III", sequence, offset, requested_length),
        ).payload
        if len(payload) < 20:
            raise ProtocolError("truncated GET_CRASH_RECORD response")
        (
            received_sequence,
            actual_offset,
            total_length,
            actual_length,
            chunk_crc32,
        ) = struct.unpack_from("<IIIII", payload)
        data = payload[20:]
        if received_sequence == 0 or actual_offset != offset:
            raise ProtocolError("GET_CRASH_RECORD identity mismatch")
        if sequence != 0 and received_sequence != sequence:
            raise ProtocolError("GET_CRASH_RECORD sequence mismatch")
        if total_length != 128 or actual_length != len(data):
            raise ProtocolError("GET_CRASH_RECORD length mismatch")
        if actual_length == 0 or actual_length > requested_length:
            raise ProtocolError("GET_CRASH_RECORD progress mismatch")
        if chunk_crc32 != crc32(data):
            raise ProtocolError("GET_CRASH_RECORD CRC mismatch")
        return CrashRecordChunk(
            sequence=received_sequence,
            actual_offset=actual_offset,
            total_length=total_length,
            actual_length=actual_length,
            chunk_crc32=chunk_crc32,
            data=data,
        )

    def download_crash_record(
        self,
        *,
        sequence: int = 0,
        chunk_bytes: int = 128,
        reconnect: Callable[[], ByteTransport] | None = None,
    ) -> bytes:
        self._require_reliability_capability()
        if not 0 <= sequence <= 0xFFFFFFFF:
            raise ValueError("CrashRecord sequence must be a uint32")
        if not 0 < chunk_bytes <= 128:
            raise ValueError(
                "CrashRecord chunk size must be between 1 and 128"
            )
        offset = 0
        record = bytearray()
        self.state = ConnectionState.TRANSFERRING
        try:
            while offset < 128:
                requested = min(chunk_bytes, 128 - offset)
                try:
                    chunk = self.get_crash_record_chunk(
                        sequence, offset, requested
                    )
                except TransportError:
                    if reconnect is None:
                        raise
                    self._reconnect(reconnect)
                    continue
                if sequence == 0:
                    sequence = chunk.sequence
                elif chunk.sequence != sequence:
                    raise ProtocolError("CrashRecord changed during download")
                record.extend(chunk.data)
                offset += chunk.actual_length
            self.state = ConnectionState.READY
            return bytes(record)
        except Exception:
            self.state = ConnectionState.ERROR
            raise

    def ack_crash_record(self, sequence: int) -> int:
        self._require_reliability_capability()
        if not 0 < sequence <= 0xFFFFFFFF:
            raise ValueError(
                "CrashRecord ACK sequence must be a nonzero uint32"
            )
        payload = self._request(
            MessageType.ACK_CRASH_RECORD,
            struct.pack("<I", sequence),
        ).payload
        if len(payload) != 4:
            raise ProtocolError("invalid ACK_CRASH_RECORD response length")
        received_sequence = struct.unpack("<I", payload)[0]
        if received_sequence != sequence:
            raise ProtocolError("ACK_CRASH_RECORD sequence mismatch")
        return received_sequence

    def read_event_chunk(self, event_id: int, offset: int,
                         requested_length: int) -> tuple[int, int, bytes]:
        payload = self._request(
            MessageType.READ_EVENT_CHUNK,
            struct.pack("<III", event_id, offset, requested_length),
        ).payload
        if len(payload) < 20:
            raise ProtocolError("truncated READ_EVENT_CHUNK response")
        received_id, actual_offset, total_length, actual_length, chunk_crc = struct.unpack_from(
            "<IIIII", payload
        )
        data = payload[20:]
        if received_id != event_id or actual_offset != offset:
            raise ProtocolError("READ_EVENT_CHUNK identity mismatch")
        if actual_length != len(data) or actual_length > requested_length:
            raise ProtocolError("READ_EVENT_CHUNK length mismatch")
        if chunk_crc != crc32(data):
            raise ProtocolError("READ_EVENT_CHUNK CRC mismatch")
        return total_length, actual_length, data

    def delete_event(self, event_id: int) -> None:
        self._request(MessageType.DELETE_EVENT, struct.pack("<I", event_id))

    def set_live(self, enabled: bool) -> None:
        message_type = MessageType.START_LIVE if enabled else MessageType.STOP_LIVE
        self._request(message_type, b"")

    @staticmethod
    def _parse_model_ota_progress(payload: bytes) -> ModelOtaProgress:
        if len(payload) != 11:
            raise ProtocolError("invalid MODEL_OTA progress response length")
        total_bytes, verified_bytes, pending_install, active_slot, model_valid = (
            struct.unpack("<IIBBB", payload)
        )
        if (
            verified_bytes > total_bytes
            or pending_install not in (0, 1)
            or active_slot not in (0, 1, 2)
            or model_valid not in (0, 1)
        ):
            raise ProtocolError("invalid MODEL_OTA progress response")
        return ModelOtaProgress(
            total_bytes=total_bytes,
            verified_bytes=verified_bytes,
            pending_install=bool(pending_install),
            active_slot=active_slot,
            model_valid=bool(model_valid),
        )

    def model_ota_begin(self, package_length: int) -> ModelOtaProgress:
        if package_length <= 0 or package_length > MODEL_OTA_MAX_PACKAGE_BYTES:
            raise ValueError(
                "model package length must be between 1 and "
                f"{MODEL_OTA_MAX_PACKAGE_BYTES} bytes"
            )
        return self._parse_model_ota_progress(
            self._request(MessageType.MODEL_OTA_BEGIN, struct.pack("<I", package_length)).payload
        )

    def model_ota_write(self, offset: int, data: bytes) -> ModelOtaProgress:
        if offset < 0 or not data:
            raise ValueError("model OTA chunk must have a nonnegative offset and data")
        return self._parse_model_ota_progress(
            self._request(
                MessageType.MODEL_OTA_WRITE_CHUNK,
                struct.pack("<II", offset, crc32(data)) + data,
            ).payload
        )

    def model_ota_query(self) -> ModelOtaProgress:
        return self._parse_model_ota_progress(
            self._request(MessageType.MODEL_OTA_QUERY, b"").payload
        )

    def model_ota_finalize(self) -> ModelOtaProgress:
        return self._parse_model_ota_progress(
            self._request(MessageType.MODEL_OTA_FINALIZE, b"").payload
        )

    def model_ota_cancel(self) -> None:
        self._request(MessageType.MODEL_OTA_CANCEL, b"")

    def upload_model_package(
        self,
        package: bytes,
        *,
        chunk_bytes: int = 256,
        progress: Callable[[ModelOtaProgress], None] | None = None,
    ) -> ModelOtaProgress:
        if not package:
            raise ValueError("model package must not be empty")
        if chunk_bytes <= 0:
            raise ValueError("model OTA chunk size must be positive")
        self.state = ConnectionState.TRANSFERRING
        try:
            current = self.model_ota_begin(len(package))
            if progress is not None:
                progress(current)
            offset = 0
            while offset < len(package):
                chunk = package[offset:offset + chunk_bytes]
                current = self.model_ota_write(offset, chunk)
                offset += len(chunk)
                if progress is not None:
                    progress(current)
            current = self.model_ota_finalize()
            if progress is not None:
                progress(current)
            self.state = ConnectionState.READY
            return current
        except Exception:
            self.state = ConnectionState.ERROR
            raise

    def download_event(
        self,
        event_id: int,
        output_path: Path,
        reconnect: Callable[[], ByteTransport] | None = None,
        chunk_bytes: int | None = None,
        cancelled: Callable[[], bool] | None = None,
        progress: Callable[[DownloadProgress], None] | None = None,
    ) -> Path:
        info = self.get_event_info(event_id)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        part_path = output_path.with_name(output_path.name + ".part")
        manifest_path = output_path.with_name(output_path.name + ".part.json")
        offset = self._prepare_partial_download(part_path, manifest_path, info)
        maximum_chunk = min(
            chunk_bytes or self.maximum_event_chunk_bytes or 4000,
            self.maximum_event_chunk_bytes or 4000,
        )
        if maximum_chunk <= 0:
            raise ProtocolError("device did not negotiate an event chunk size")

        self.state = ConnectionState.TRANSFERRING
        try:
            with part_path.open("ab") as event_file:
                while offset < info.total_length:
                    if cancelled is not None and cancelled():
                        raise DownloadCancelled("event download cancelled")
                    requested = min(maximum_chunk, info.total_length - offset)
                    try:
                        total_length, actual_length, data = self.read_event_chunk(
                            event_id, offset, requested
                        )
                    except TransportError:
                        if reconnect is None:
                            raise
                        self._reconnect(reconnect)
                        resumed_info = self.get_event_info(event_id)
                        if resumed_info != info:
                            raise ProtocolError("event changed while resuming download")
                        continue
                    if total_length != info.total_length or actual_length == 0:
                        raise ProtocolError("invalid READ_EVENT_CHUNK progress")
                    event_file.write(data)
                    event_file.flush()
                    offset += actual_length
                    self._write_manifest(manifest_path, info, offset)
                    if progress is not None:
                        progress(
                            DownloadProgress(
                                event_id=event_id,
                                total_bytes=info.total_length,
                                verified_bytes=offset,
                            )
                        )

            if self._file_crc32(part_path) != info.event_crc32:
                raise ProtocolError("downloaded event CRC mismatch")
            os.replace(part_path, output_path)
            manifest_path.unlink(missing_ok=True)
            self.state = ConnectionState.READY
            return output_path
        except DownloadCancelled:
            self.state = ConnectionState.READY
            raise
        except Exception:
            self.state = ConnectionState.ERROR
            raise

    def close(self) -> None:
        self.transport.close()
        self.state = ConnectionState.DISCONNECTED

    def _request(self, message_type: MessageType, payload: bytes) -> Frame:
        if self.state in {ConnectionState.DISCONNECTED, ConnectionState.ERROR,
                          ConnectionState.INCOMPATIBLE}:
            raise TransportError(f"connection is not ready: {self.state.value}")
        sequence = self._allocate_sequence()
        request = Frame(int(message_type), 0, sequence, payload)
        try:
            self._trace_frame("TX", request)
            self.transport.write(encode_frame(request))
            deadline = time.monotonic() + self.timeout_seconds
            while time.monotonic() < deadline:
                data = self.transport.read(512)
                if not data:
                    continue
                for response in self._parser.feed(data):
                    self._trace_frame("RX", response)
                    if response.sequence != sequence:
                        continue
                    if response.message_type == MessageType.ERROR:
                        self._raise_device_error(response)
                    if response.message_type != response_type(int(message_type)):
                        raise ProtocolError("unexpected TERP response type")
                    return response
        except TransportError:
            self.state = ConnectionState.DISCONNECTED
            raise
        self.state = ConnectionState.ERROR
        raise TransportError("TERP response timed out")

    def _require_reliability_capability(self) -> None:
        if (
            self.capability_flags
            & TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1
        ) == 0:
            raise CapabilityUnavailableError(
                "device does not advertise reliability evidence capability"
            )

    def _reconnect(self, reconnect: Callable[[], ByteTransport]) -> None:
        self.transport.close()
        self.transport = reconnect()
        self._parser = StreamingParser()
        self._next_sequence = 1
        self.state = ConnectionState.OPENING
        self.hello()

    def _allocate_sequence(self) -> int:
        sequence = self._next_sequence
        self._next_sequence = (self._next_sequence + 1) & 0xFFFFFFFF
        if self._next_sequence == 0:
            self._next_sequence = 1
        return sequence

    @staticmethod
    def _raise_device_error(response: Frame) -> None:
        if len(response.payload) != 4:
            raise ProtocolError("malformed TERP ERROR response")
        code, request_type = struct.unpack("<HH", response.payload)
        raise DeviceError(code, request_type)

    @staticmethod
    def _file_crc32(path: Path) -> int:
        checksum = 0
        with path.open("rb") as event_file:
            while chunk := event_file.read(64 * 1024):
                checksum = zlib.crc32(chunk, checksum)
        return checksum & 0xFFFFFFFF

    @staticmethod
    def _write_manifest(path: Path, info: EventInfo, offset: int) -> None:
        path.write_text(
            json.dumps({**asdict(info), "verified_offset": offset}, sort_keys=True),
            encoding="utf-8",
        )

    @classmethod
    def _prepare_partial_download(cls, part_path: Path, manifest_path: Path,
                                  info: EventInfo) -> int:
        if not part_path.exists() or not manifest_path.exists():
            part_path.unlink(missing_ok=True)
            manifest_path.unlink(missing_ok=True)
            part_path.touch()
            cls._write_manifest(manifest_path, info, 0)
            return 0
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            offset = int(manifest["verified_offset"])
            expected = {**asdict(info), "verified_offset": offset}
            if manifest != expected or offset != part_path.stat().st_size:
                raise ValueError("partial download metadata does not match")
            return offset
        except (KeyError, OSError, ValueError, json.JSONDecodeError):
            part_path.unlink(missing_ok=True)
            manifest_path.unlink(missing_ok=True)
            part_path.touch()
            cls._write_manifest(manifest_path, info, 0)
            return 0
