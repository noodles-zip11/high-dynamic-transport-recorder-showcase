"""Deterministic in-memory TERP device used only by host software tests."""

from __future__ import annotations

import struct
from typing import Iterable

from .client import ByteTransport, TransportDisconnected
from .codec import Frame, crc32, encode_frame
from .messages import (
    TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1,
    ErrorCode,
    MessageType,
    response_type,
)
from .parser import StreamingParser


class SimulatedDevice:
    def __init__(
        self,
        events: dict[int, bytes],
        *,
        reliability_enabled: bool = False,
        event_evidence: dict[int, bytes] | None = None,
        crash_record: bytes | None = None,
    ) -> None:
        self.events = dict(events)
        self.reliability_enabled = reliability_enabled
        self.event_evidence = dict(event_evidence or {})
        if crash_record is not None and len(crash_record) != 128:
            raise ValueError("simulated CrashRecord must be 128 bytes")
        self.crash_record = (
            bytes(crash_record) if crash_record is not None else None
        )
        self.crash_ack_sequence = 0
        self.utc_unix_seconds = 1_700_000_000
        self.epoch_id = 1
        self.session_ready = False

    def handle(self, request: Frame) -> Frame:
        try:
            message_type = MessageType(request.message_type)
        except ValueError:
            return self._error(request, ErrorCode.UNSUPPORTED)
        if message_type is MessageType.HELLO:
            if len(request.payload) != 1:
                return self._error(request, ErrorCode.MALFORMED)
            if request.payload != b"\x01":
                return self._error(request, ErrorCode.INCOMPATIBLE)
            self.session_ready = True
            capabilities = 0x0B
            if self.reliability_enabled:
                capabilities |= TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1
            return self._response(
                request, struct.pack("<BIII", 1, 4096, 4000, capabilities)
            )
        if not self.session_ready:
            return self._error(request, ErrorCode.HANDSHAKE_REQUIRED)
        if message_type is MessageType.GET_DEVICE_INFO:
            return self._response(
                request,
                self._text("STM32H743")
                + self._text("phase08-sim")
                + self._text("usb-pending")
                + self._text("SIM-0001")
                + struct.pack(
                    "<I",
                    0x0F
                    | (
                        TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1
                        if self.reliability_enabled
                        else 0
                    ),
                ),
            )
        if message_type is MessageType.GET_HEALTH:
            return self._response(request, struct.pack("<BBIII", 1, 1, 8_000_000, 0, 0))
        if message_type is MessageType.GET_TIME:
            return self._response(request, struct.pack("<qI", self.utc_unix_seconds,
                                                        self.epoch_id))
        if message_type is MessageType.SET_TIME:
            if len(request.payload) != 8:
                return self._error(request, ErrorCode.MALFORMED)
            self.utc_unix_seconds = struct.unpack("<q", request.payload)[0]
            self.epoch_id += 1
            return self._response(request, struct.pack("<qI", self.utc_unix_seconds,
                                                        self.epoch_id))
        if message_type is MessageType.LIST_EVENTS:
            return self._list_events(request)
        if message_type is MessageType.GET_EVENT_INFO:
            return self._event_info(request)
        if message_type is MessageType.READ_EVENT_CHUNK:
            return self._read_event_chunk(request)
        if message_type is MessageType.GET_EVENT_EVIDENCE:
            return self._get_event_evidence(request)
        if message_type is MessageType.GET_CRASH_RECORD:
            return self._get_crash_record(request)
        if message_type is MessageType.ACK_CRASH_RECORD:
            return self._ack_crash_record(request)
        return self._error(request, ErrorCode.UNSUPPORTED)

    def _list_events(self, request: Frame) -> Frame:
        if len(request.payload) != 8:
            return self._error(request, ErrorCode.MALFORMED)
        after_event_id, maximum_count = struct.unpack("<II", request.payload)
        selected = [event_id for event_id in sorted(self.events)
                    if event_id > after_event_id][:min(maximum_count or 16, 16)]
        next_event = selected[-1] if len(selected) == min(maximum_count or 16, 16) else 0
        payload = struct.pack("<IH", next_event, len(selected))
        for event_id in selected:
            event = self.events[event_id]
            payload += struct.pack("<III", event_id, len(event), crc32(event))
        return self._response(request, payload)

    def _event_info(self, request: Frame) -> Frame:
        if len(request.payload) != 4:
            return self._error(request, ErrorCode.MALFORMED)
        event_id = struct.unpack("<I", request.payload)[0]
        event = self.events.get(event_id)
        if event is None:
            return self._error(request, ErrorCode.NOT_FOUND)
        return self._response(request, struct.pack("<III", event_id, len(event), crc32(event)))

    def _read_event_chunk(self, request: Frame) -> Frame:
        if len(request.payload) != 12:
            return self._error(request, ErrorCode.MALFORMED)
        event_id, offset, requested_length = struct.unpack("<III", request.payload)
        event = self.events.get(event_id)
        if event is None:
            return self._error(request, ErrorCode.NOT_FOUND)
        if requested_length == 0 or requested_length > 4000 or offset > len(event):
            return self._error(request, ErrorCode.MALFORMED)
        data = event[offset:offset + requested_length]
        payload = struct.pack("<IIIII", event_id, offset, len(event), len(data), crc32(data))
        return self._response(request, payload + data)

    def _get_event_evidence(self, request: Frame) -> Frame:
        if len(request.payload) != 4:
            return self._error(request, ErrorCode.MALFORMED)
        if not self.reliability_enabled:
            return self._error(request, ErrorCode.UNSUPPORTED)
        event_id = struct.unpack("<I", request.payload)[0]
        payload = self.event_evidence.get(event_id)
        if event_id not in self.events or payload is None:
            return self._error(request, ErrorCode.NOT_FOUND)
        if len(payload) != 20:
            return self._error(request, ErrorCode.INTERNAL)
        return self._response(request, payload)

    def _get_crash_record(self, request: Frame) -> Frame:
        if len(request.payload) != 12:
            return self._error(request, ErrorCode.MALFORMED)
        if not self.reliability_enabled:
            return self._error(request, ErrorCode.UNSUPPORTED)
        sequence, offset, requested_length = struct.unpack(
            "<III", request.payload
        )
        if (
            requested_length == 0
            or requested_length > 4000
            or offset >= 128
            or offset > 0xFFFFFFFF - requested_length
        ):
            return self._error(request, ErrorCode.MALFORMED)
        if self.crash_record is None:
            return self._error(request, ErrorCode.NOT_FOUND)
        record_sequence = struct.unpack_from("<I", self.crash_record, 12)[0]
        if record_sequence == 0 or (
            sequence != 0 and sequence != record_sequence
        ):
            return self._error(request, ErrorCode.NOT_FOUND)
        data = self.crash_record[offset:offset + requested_length]
        payload = struct.pack(
            "<IIIII",
            record_sequence,
            offset,
            128,
            len(data),
            crc32(data),
        )
        return self._response(request, payload + data)

    def _ack_crash_record(self, request: Frame) -> Frame:
        if len(request.payload) != 4:
            return self._error(request, ErrorCode.MALFORMED)
        if not self.reliability_enabled:
            return self._error(request, ErrorCode.UNSUPPORTED)
        sequence = struct.unpack("<I", request.payload)[0]
        if sequence == 0:
            return self._error(request, ErrorCode.MALFORMED)
        if self.crash_record is None:
            return self._error(request, ErrorCode.NOT_FOUND)
        record_sequence = struct.unpack_from("<I", self.crash_record, 12)[0]
        if sequence != record_sequence:
            return self._error(request, ErrorCode.NOT_FOUND)
        self.crash_ack_sequence = sequence
        return self._response(request, struct.pack("<I", sequence))

    @staticmethod
    def _text(value: str) -> bytes:
        encoded = value.encode("utf-8")
        return bytes([len(encoded)]) + encoded

    @staticmethod
    def _response(request: Frame, payload: bytes) -> Frame:
        return Frame(response_type(request.message_type), 0x8000, request.sequence, payload)

    @staticmethod
    def _error(request: Frame, code: ErrorCode) -> Frame:
        return Frame(int(MessageType.ERROR), 0x8000, request.sequence,
                     struct.pack("<HH", int(code), request.message_type))


class MemoryTransport(ByteTransport):
    def __init__(self, device: SimulatedDevice) -> None:
        self.device = device
        self._request_parser = StreamingParser()
        self._response_bytes = bytearray()
        self._closed = False

    def write(self, data: bytes) -> None:
        if self._closed:
            raise TransportDisconnected("simulated transport is closed")
        for request in self._request_parser.feed(data):
            self._response_bytes.extend(encode_frame(self.device.handle(request)))

    def read(self, maximum_bytes: int) -> bytes:
        if self._closed:
            raise TransportDisconnected("simulated transport is closed")
        if not self._response_bytes:
            return b""
        count = min(maximum_bytes, 97, len(self._response_bytes))
        result = bytes(self._response_bytes[:count])
        del self._response_bytes[:count]
        return result

    def close(self) -> None:
        self._closed = True


class DisconnectingTransportFactory:
    """Disconnects the first N chunk requests, preserving device-side data."""

    def __init__(self, device: SimulatedDevice, disconnect_count: int) -> None:
        self.device = device
        self.remaining_disconnects = disconnect_count
        self.disconnects_seen = 0

    def create(self) -> ByteTransport:
        return _DisconnectingMemoryTransport(self.device, self)


class _DisconnectingMemoryTransport(MemoryTransport):
    def __init__(self, device: SimulatedDevice,
                 factory: DisconnectingTransportFactory) -> None:
        super().__init__(device)
        self._factory = factory

    def write(self, data: bytes) -> None:
        if self._closed:
            raise TransportDisconnected("simulated transport is closed")
        requests = self._request_parser.feed(data)
        for request in requests:
            if (request.message_type == MessageType.READ_EVENT_CHUNK
                    and self._factory.remaining_disconnects > 0):
                self._factory.remaining_disconnects -= 1
                self._factory.disconnects_seen += 1
                self._closed = True
                raise TransportDisconnected("scripted event-download disconnect")
            self._response_bytes.extend(encode_frame(self.device.handle(request)))
