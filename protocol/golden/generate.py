"""Generate the checked-in TERP v1 binary golden vectors."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "host"))

from transport_recorder.protocol.codec import Frame, crc32, encode_frame
from transport_recorder.protocol.messages import MessageType, response_type


GOLDEN_DIRECTORY = Path(__file__).resolve().parent


def _text(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return bytes([len(encoded)]) + encoded


def vectors() -> dict[str, bytes]:
    device_info_payload = (
        _text("STM32H743")
        + _text("phase08-sim")
        + _text("usb-pending")
        + _text("SIM-0001")
        + struct.pack("<I", 0x0F)
    )
    event_data = b"\xA0\xA1\xA2\xA3\xA4\xA5"
    event_chunk_payload = struct.pack(
        "<IIIII", 42, 1024, 1030, len(event_data), crc32(event_data)
    ) + event_data
    return {
        "get_device_info_request.bin": encode_frame(
            Frame(MessageType.GET_DEVICE_INFO, 0, 1, b"")
        ),
        "get_device_info_response.bin": encode_frame(
            Frame(response_type(MessageType.GET_DEVICE_INFO), 0x8000, 1,
                  device_info_payload)
        ),
        "event_chunk.bin": encode_frame(
            Frame(response_type(MessageType.READ_EVENT_CHUNK), 0x8000, 7,
                  event_chunk_payload)
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    mismatches = []
    for filename, content in vectors().items():
        path = GOLDEN_DIRECTORY / filename
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                mismatches.append(filename)
        else:
            path.write_bytes(content)
        print(f"{filename} {len(content)} {hashlib.sha256(content).hexdigest()}")
    if mismatches:
        raise SystemExit("golden vectors differ: " + ", ".join(mismatches))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
