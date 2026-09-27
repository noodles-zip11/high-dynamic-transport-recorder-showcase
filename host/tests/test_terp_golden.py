from pathlib import Path
import struct

import pytest

from host.transport_recorder.protocol.codec import ProtocolError, decode_frame, encode_frame
from host.transport_recorder.protocol.messages import MessageType, response_type


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GOLDEN_DIRECTORY = REPOSITORY_ROOT / "protocol" / "golden"


def test_python_encoder_matches_the_c_shared_get_device_info_vector() -> None:
    golden = (GOLDEN_DIRECTORY / "get_device_info_request.bin").read_bytes()

    decoded = decode_frame(golden)

    assert decoded.message_type == MessageType.GET_DEVICE_INFO
    assert decoded.sequence == 1
    assert decoded.payload == b""
    assert encode_frame(decoded) == golden


def test_python_decoder_reads_the_shared_event_chunk_vector() -> None:
    decoded = decode_frame((GOLDEN_DIRECTORY / "event_chunk.bin").read_bytes())
    event_id, offset, total, length, chunk_crc = struct.unpack_from("<IIIII", decoded.payload)

    assert decoded.message_type == response_type(MessageType.READ_EVENT_CHUNK)
    assert (event_id, offset, total, length) == (42, 1024, 1030, 6)
    assert decoded.payload[20:] == b"\xA0\xA1\xA2\xA3\xA4\xA5"
    assert chunk_crc != 0


@pytest.mark.parametrize("filename", [
    "get_device_info_request.bin",
    "get_device_info_response.bin",
    "event_chunk.bin",
])
def test_a_one_byte_golden_vector_mutation_is_rejected(filename: str) -> None:
    corrupt = bytearray((GOLDEN_DIRECTORY / filename).read_bytes())
    corrupt[-1] ^= 0x01

    with pytest.raises(ProtocolError):
        decode_frame(bytes(corrupt))
