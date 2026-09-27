import pytest
import random

from host.transport_recorder.protocol.codec import (
    Frame,
    ProtocolError,
    decode_frame,
    encode_frame,
)
from host.transport_recorder.protocol.messages import MessageType, RESPONSE_BIT
from host.transport_recorder.protocol.parser import StreamingParser


def test_round_trips_a_minimal_get_device_info_request() -> None:
    frame = Frame(
        message_type=MessageType.GET_DEVICE_INFO,
        flags=0,
        sequence=1,
        payload=b"",
    )

    encoded = encode_frame(frame)

    assert len(encoded) == 24
    assert decode_frame(encoded) == frame


def test_streaming_parser_delivers_a_frame_one_byte_at_a_time() -> None:
    frame = Frame(MessageType.HELLO, 0, 0x10203040, b"\x01\x00")
    parser = StreamingParser()
    received = []

    for byte in encode_frame(frame):
        received.extend(parser.feed(bytes([byte])))

    assert received == [frame]


def test_decoder_rejects_a_payload_crc_error() -> None:
    encoded = bytearray(encode_frame(Frame(MessageType.HELLO, 0, 1, b"ok")))
    encoded[-1] ^= 0x01

    with pytest.raises(ProtocolError, match="payload CRC"):
        decode_frame(bytes(encoded))


def test_streaming_parser_recovers_a_valid_frame_inside_a_corrupt_payload() -> None:
    inner = encode_frame(Frame(MessageType.GET_DEVICE_INFO, 0, 0x55667788, b""))
    outer = bytearray(encode_frame(Frame(MessageType.HELLO, 0, 1, inner)))
    outer[-1] ^= 0x01

    parser = StreamingParser()
    received = parser.feed(bytes(outer))

    assert parser.stats.payload_crc_error_count == 1
    assert received == [Frame(MessageType.GET_DEVICE_INFO, 0, 0x55667788, b"")]


def test_streaming_parser_does_not_classify_a_structured_error_as_unknown() -> None:
    parser = StreamingParser()
    error = Frame(MessageType.ERROR, RESPONSE_BIT, 2, b"\x02\x00\x09\x00")

    assert parser.feed(encode_frame(error)) == [error]
    assert parser.stats.unknown_message_count == 0


def test_streaming_parser_recovers_from_one_thousand_corrupted_frames() -> None:
    valid = encode_frame(Frame(MessageType.GET_DEVICE_INFO, 0, 99, b""))
    randomizer = random.Random(20260731)

    for index in range(1000):
        parser = StreamingParser()
        corrupt = bytearray(valid)
        corrupt[randomizer.randrange(len(corrupt))] ^= randomizer.randrange(1, 256)
        received = parser.feed(bytes(corrupt))
        received.extend(parser.feed(valid))

        assert received == [Frame(MessageType.GET_DEVICE_INFO, 0, 99, b"")]
        assert len(parser._buffer) <= 1, index
