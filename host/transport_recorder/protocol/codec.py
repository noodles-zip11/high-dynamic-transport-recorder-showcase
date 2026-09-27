"""Binary TERP v1 frame codec."""

from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib


SYNC = b"TR"
VERSION = 1
HEADER_LENGTH = 20
MAX_PAYLOAD_LENGTH = 4096
HEADER_PREFIX_FORMAT = "<2sBBHHII"
HEADER_FORMAT = "<2sBBHHIII"
FRAME_OVERHEAD = HEADER_LENGTH + 4


class ProtocolError(ValueError):
    """Raised when a TERP frame is malformed or corrupt."""


@dataclass(frozen=True)
class Frame:
    message_type: int
    flags: int
    sequence: int
    payload: bytes


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def encode_frame(frame: Frame) -> bytes:
    if not 0 <= int(frame.message_type) <= 0xFFFF:
        raise ProtocolError("message type out of range")
    if not 0 <= frame.flags <= 0xFFFF:
        raise ProtocolError("flags out of range")
    if not 0 <= frame.sequence <= 0xFFFFFFFF:
        raise ProtocolError("sequence out of range")
    if len(frame.payload) > MAX_PAYLOAD_LENGTH:
        raise ProtocolError("payload exceeds TERP maximum")

    prefix = struct.pack(
        HEADER_PREFIX_FORMAT,
        SYNC,
        VERSION,
        HEADER_LENGTH,
        int(frame.message_type),
        frame.flags,
        frame.sequence,
        len(frame.payload),
    )
    header = prefix + struct.pack("<I", crc32(prefix[2:]))
    return header + frame.payload + struct.pack("<I", crc32(frame.payload))


def decode_frame(encoded: bytes) -> Frame:
    if len(encoded) < FRAME_OVERHEAD:
        raise ProtocolError("frame is shorter than TERP overhead")

    sync, version, header_length, message_type, flags, sequence, payload_length, header_crc = (
        struct.unpack_from(HEADER_FORMAT, encoded)
    )
    if sync != SYNC:
        raise ProtocolError("sync mismatch")
    if version != VERSION:
        raise ProtocolError("unsupported TERP version")
    if header_length != HEADER_LENGTH:
        raise ProtocolError("header length mismatch")
    if payload_length > MAX_PAYLOAD_LENGTH:
        raise ProtocolError("payload exceeds TERP maximum")
    if header_crc != crc32(encoded[2:16]):
        raise ProtocolError("header CRC mismatch")

    expected_length = HEADER_LENGTH + payload_length + 4
    if len(encoded) != expected_length:
        raise ProtocolError("frame length mismatch")

    payload = encoded[HEADER_LENGTH:-4]
    payload_crc = struct.unpack_from("<I", encoded, HEADER_LENGTH + payload_length)[0]
    if payload_crc != crc32(payload):
        raise ProtocolError("payload CRC mismatch")
    return Frame(message_type, flags, sequence, payload)
