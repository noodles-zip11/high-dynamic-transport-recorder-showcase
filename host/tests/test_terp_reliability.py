import struct

import pytest

from host.transport_recorder.protocol.client import (
    CapabilityUnavailableError,
    DeviceError,
    ProtocolError,
    ReliabilityAiDecision,
    ReliabilityVerdict,
    TerpClient,
    TransportDisconnected,
)
from host.transport_recorder.protocol.codec import Frame
from host.transport_recorder.protocol.messages import (
    ErrorCode,
    MessageType,
)
from host.transport_recorder.protocol.simulated_device import (
    MemoryTransport,
    SimulatedDevice,
)


def evidence_payload(
    event_id: int,
    verdict: int,
    *,
    ai_decision: int = 0,
    reason_flags: int = 0,
) -> bytes:
    return struct.pack(
        "<I H B B I B B H I",
        event_id,
        1,
        verdict,
        ai_decision,
        reason_flags,
        1,
        0,
        0,
        0,
    )


def make_crash_record(sequence: int = 17) -> bytes:
    record = bytearray(range(128))
    struct.pack_into("<I", record, 12, sequence)
    return bytes(record)


def make_client(device: SimulatedDevice) -> TerpClient:
    client = TerpClient(MemoryTransport(device))
    client.hello()
    return client


class CountingDevice(SimulatedDevice):
    def __init__(self, *args: object, **kwargs: object) -> None:
        super().__init__(*args, **kwargs)
        self.seen_messages: list[int] = []

    def handle(self, request: Frame) -> Frame:
        self.seen_messages.append(request.message_type)
        return super().handle(request)


class MutatingCrashDevice(SimulatedDevice):
    def __init__(self, mutation: str) -> None:
        super().__init__(
            {},
            reliability_enabled=True,
            crash_record=make_crash_record(),
        )
        self.mutation = mutation

    def _get_crash_record(self, request: Frame) -> Frame:
        response = super()._get_crash_record(request)
        payload = bytearray(response.payload)
        if self.mutation == "crc":
            payload[16] ^= 0x01
        elif self.mutation == "identity":
            payload[4] ^= 0x01
        return self._response(request, bytes(payload))


class DisconnectingCrashTransport(MemoryTransport):
    def __init__(
        self, device: SimulatedDevice, factory: "CrashFactory"
    ) -> None:
        super().__init__(device)
        self.factory = factory

    def write(self, data: bytes) -> None:
        if self._closed:
            raise TransportDisconnected("simulated transport is closed")
        requests = self._request_parser.feed(data)
        for request in requests:
            if (
                request.message_type == MessageType.GET_CRASH_RECORD
                and self.factory.remaining_disconnects > 0
            ):
                self.factory.remaining_disconnects -= 1
                self._closed = True
                raise TransportDisconnected(
                    "scripted crash-download disconnect"
                )
            self._response_bytes.extend(
                self._encode_response(self.device.handle(request))
            )

    @staticmethod
    def _encode_response(response: Frame) -> bytes:
        from host.transport_recorder.protocol.codec import encode_frame

        return encode_frame(response)


class CrashFactory:
    def __init__(self, device: SimulatedDevice, disconnect_count: int) -> None:
        self.device = device
        self.remaining_disconnects = disconnect_count

    def create(self) -> MemoryTransport:
        return DisconnectingCrashTransport(self.device, self)


def test_old_device_gates_all_reliability_calls_without_sending_them() -> None:
    device = CountingDevice({42: b"event"})
    client = make_client(device)

    assert client.capability_flags & 0x100 == 0
    with pytest.raises(CapabilityUnavailableError):
        client.get_event_evidence(42)
    with pytest.raises(CapabilityUnavailableError):
        client.get_crash_record_chunk(0, 0, 16)
    with pytest.raises(CapabilityUnavailableError):
        client.ack_crash_record(17)
    assert MessageType.GET_EVENT_EVIDENCE not in device.seen_messages
    assert MessageType.GET_CRASH_RECORD not in device.seen_messages
    assert MessageType.ACK_CRASH_RECORD not in device.seen_messages


def test_enabled_device_displays_non_pass_evidence_without_ai_prediction(
) -> None:
    device = SimulatedDevice(
        {42: b"event", 43: b"event"},
        reliability_enabled=True,
        event_evidence={
            42: evidence_payload(
                42, ReliabilityVerdict.DEGRADED, reason_flags=1
            ),
            43: evidence_payload(
                43, ReliabilityVerdict.INVALID, reason_flags=12
            ),
        },
    )
    client = make_client(device)

    degraded = client.get_event_evidence(42)
    invalid = client.get_event_evidence(43)

    assert degraded.verdict is ReliabilityVerdict.DEGRADED
    assert degraded.ai_decision is ReliabilityAiDecision.NOT_RUN_QUALITY
    assert degraded.reason_flags == 1
    assert invalid.verdict is ReliabilityVerdict.INVALID
    assert invalid.ai_decision is ReliabilityAiDecision.NOT_RUN_QUALITY


def test_crash_download_preserves_raw_bytes_and_ack_is_exact_and_idempotent(
) -> None:
    raw = make_crash_record()
    device = SimulatedDevice(
        {},
        reliability_enabled=True,
        crash_record=raw,
    )
    client = make_client(device)

    assert client.download_crash_record(chunk_bytes=17) == raw
    assert client.ack_crash_record(17) == 17
    assert client.ack_crash_record(17) == 17
    with pytest.raises(DeviceError) as raised:
        client.ack_crash_record(18)
    assert raised.value.code is ErrorCode.NOT_FOUND


def test_crash_download_honors_explicit_sequence() -> None:
    raw = make_crash_record(sequence=17)
    device = SimulatedDevice(
        {},
        reliability_enabled=True,
        crash_record=raw,
    )
    client = make_client(device)

    assert client.download_crash_record(sequence=17) == raw
    with pytest.raises(DeviceError) as raised:
        client.download_crash_record(sequence=18)
    assert raised.value.code is ErrorCode.NOT_FOUND


@pytest.mark.parametrize("mutation", ["crc", "identity"])
def test_crash_chunk_rejects_crc_or_identity_mismatch(mutation: str) -> None:
    client = make_client(MutatingCrashDevice(mutation))

    with pytest.raises(ProtocolError):
        client.get_crash_record_chunk(0, 0, 32)


def test_crash_download_resumes_after_disconnect_using_resolved_sequence(
) -> None:
    raw = make_crash_record()
    device = SimulatedDevice({}, reliability_enabled=True, crash_record=raw)
    factory = CrashFactory(device, disconnect_count=1)
    client = TerpClient(factory.create())
    client.hello()

    assert (
        client.download_crash_record(
            chunk_bytes=32,
            reconnect=factory.create,
        )
        == raw
    )
    assert device.crash_ack_sequence == 0
