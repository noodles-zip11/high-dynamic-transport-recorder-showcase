from __future__ import annotations

import re
from hashlib import sha256
from pathlib import Path

import yaml

from host.transport_recorder.protocol.codec import decode_frame, encode_frame
from host.transport_recorder.protocol.messages import (
    MessageType,
    TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1,
)


PROJECT_ROOT = Path(__file__).resolve().parents[2]
TERP_MESSAGES = PROJECT_ROOT / "protocol" / "terp_messages.yaml"
GOLDEN_DIRECTORY = PROJECT_ROOT / "protocol" / "golden"

RELIABILITY_MESSAGES = {
    "GET_EVENT_EVIDENCE": {
        "id": 0x0300,
        "name": "GET_EVENT_EVIDENCE",
        "request": ["event_id_u32"],
        "response": [
            "event_id_u32",
            "evidence_version_u16",
            "verdict_u8",
            "ai_decision_u8",
            "reason_flags_u32",
            "storage_state_u8",
            "ai_result_status_u8",
            "ai_failure_reason_u16",
            "ai_result_sequence_u32",
        ],
    },
    "GET_CRASH_RECORD": {
        "id": 0x0301,
        "name": "GET_CRASH_RECORD",
        "request": ["sequence_u32", "offset_u32", "requested_length_u32"],
        "response": [
            "sequence_u32",
            "actual_offset_u32",
            "total_length_u32",
            "actual_length_u32",
            "chunk_crc32_u32",
            "data_bytes",
        ],
    },
    "ACK_CRASH_RECORD": {
        "id": 0x0302,
        "name": "ACK_CRASH_RECORD",
        "request": ["sequence_u32"],
        "response": ["sequence_u32"],
    },
}

LEGACY_GOLDEN_SHA256 = {
    "event_chunk.bin": (
        "a44ae27416b3fd3a3754c047ce0dfb37ff5c141855a147fb69043d6911fe6c0d"
    ),
    "get_device_info_request.bin": (
        "b9d2c7d17e676c56d86ea1b6e0da0effa7bbc013580742ae9b1e65861c0b1912"
    ),
    "get_device_info_response.bin": (
        "86eea2e240e8c6e4af8c5c7408310000b4ac6897dfd56213790d02478787b720"
    ),
}

RELIABILITY_GOLDENS = (
    "get_event_evidence_request.hex",
    "get_event_evidence_response.hex",
    "get_crash_record_request.hex",
    "get_crash_record_response.hex",
    "ack_crash_record_request.hex",
    "ack_crash_record_response.hex",
    "reliability_error_unsupported.hex",
    "reliability_error_not_found.hex",
    "reliability_error_malformed.hex",
    "reliability_error_handshake_required.hex",
)


def test_reliability_registry_is_additive_and_exact() -> None:
    document = yaml.safe_load(TERP_MESSAGES.read_text(encoding="utf-8"))
    messages = document["messages"]
    by_name = {message["name"]: message for message in messages}

    assert set(RELIABILITY_MESSAGES).issubset(by_name)
    assert len({message["id"] for message in messages}) == len(messages)
    for name, expected in RELIABILITY_MESSAGES.items():
        assert by_name[name] == expected
        assert MessageType[name].value == expected["id"]

    device_header = (
        PROJECT_ROOT / "firmware" / "components" / "protocol"
        / "terp_device.h"
    ).read_text(encoding="utf-8")
    assert "TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1" in device_header
    assert TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1 == 0x100
    assert re.search(
        r"#define\s+TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1\s+"
        r"\(UINT32_C\(1\)\s*<<\s*8\)",
        device_header,
    )


def test_reliability_golden_frames_are_literal_and_round_trip() -> None:
    for filename in RELIABILITY_GOLDENS:
        path = GOLDEN_DIRECTORY / filename
        encoded = bytes.fromhex(path.read_text(encoding="ascii"))
        assert encode_frame(decode_frame(encoded)) == encoded


def test_existing_terp_golden_files_are_unchanged() -> None:
    for filename, expected_digest in LEGACY_GOLDEN_SHA256.items():
        digest = sha256((GOLDEN_DIRECTORY / filename).read_bytes()).hexdigest()
        assert digest == expected_digest
