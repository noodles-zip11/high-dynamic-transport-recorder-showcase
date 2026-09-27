"""Bounded streaming TERP v1 parser."""

from __future__ import annotations

from dataclasses import dataclass
import struct

from .codec import (
    FRAME_OVERHEAD,
    HEADER_LENGTH,
    HEADER_PREFIX_FORMAT,
    MAX_PAYLOAD_LENGTH,
    SYNC,
    VERSION,
    Frame,
    crc32,
    decode_frame,
)
from .messages import is_known_message_type


@dataclass
class ParserStats:
    header_crc_error_count: int = 0
    payload_crc_error_count: int = 0
    length_error_count: int = 0
    version_error_count: int = 0
    unknown_message_count: int = 0
    resync_byte_count: int = 0
    timeout_count: int = 0


class StreamingParser:
    """Accept arbitrary byte chunks without unbounded buffering."""

    def __init__(self) -> None:
        self.stats = ParserStats()
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        frames: list[Frame] = []
        for byte in data:
            self._buffer.append(byte)
            frames.extend(self._drain())
        return frames

    def on_timeout(self) -> None:
        if self._buffer:
            self.stats.timeout_count += 1
            self.stats.resync_byte_count += len(self._buffer)
            self._buffer.clear()

    def _drain(self) -> list[Frame]:
        frames: list[Frame] = []
        while True:
            if not self._align_to_sync():
                return frames
            if len(self._buffer) < HEADER_LENGTH:
                return frames

            _, version, header_length, message_type, _, _, payload_length = struct.unpack_from(
                HEADER_PREFIX_FORMAT, self._buffer
            )
            header_crc = struct.unpack_from("<I", self._buffer, 16)[0]
            if version != VERSION:
                self.stats.version_error_count += 1
                self._discard_one_byte()
                continue
            if header_length != HEADER_LENGTH or payload_length > MAX_PAYLOAD_LENGTH:
                self.stats.length_error_count += 1
                self._discard_one_byte()
                continue
            if header_crc != crc32(bytes(self._buffer[2:16])):
                self.stats.header_crc_error_count += 1
                self._discard_one_byte()
                continue

            frame_length = FRAME_OVERHEAD + payload_length
            if len(self._buffer) < frame_length:
                return frames
            candidate = bytes(self._buffer[:frame_length])
            try:
                frame = decode_frame(candidate)
            except ValueError as error:
                if "payload CRC" in str(error):
                    self.stats.payload_crc_error_count += 1
                else:
                    self.stats.length_error_count += 1
                self._discard_one_byte()
                continue

            del self._buffer[:frame_length]
            if not is_known_message_type(frame.message_type):
                self.stats.unknown_message_count += 1
            frames.append(frame)

    def _align_to_sync(self) -> bool:
        index = self._buffer.find(SYNC)
        if index == 0:
            return True
        if index > 0:
            self.stats.resync_byte_count += index
            del self._buffer[:index]
            return True
        if self._buffer[-1:] == SYNC[:1]:
            self.stats.resync_byte_count += len(self._buffer) - 1
            del self._buffer[:-1]
        else:
            self.stats.resync_byte_count += len(self._buffer)
            self._buffer.clear()
        return False

    def _discard_one_byte(self) -> None:
        del self._buffer[:1]
        self.stats.resync_byte_count += 1
