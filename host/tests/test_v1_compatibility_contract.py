from __future__ import annotations

from dataclasses import fields
from hashlib import sha256
from pathlib import Path
import struct
import subprocess
import sys
import zlib

import pytest
import yaml

from host.tools.capture_debug_event import EventFormatError, parse_ev03
from host.transport_recorder.analysis.event_record import load_event
from host.transport_recorder.protocol.messages_generated import MessageType
from host.transport_recorder.repository.event_repository import EventRepository


PROJECT_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIRECTORY = PROJECT_ROOT / "tools"
TERP_MESSAGES_PATH = PROJECT_ROOT / "protocol" / "terp_messages.yaml"
MEMORY_LAYOUT_PATH = PROJECT_ROOT / "config" / "memory_layout.yaml"
EV03_FIXTURE_PATH = (
    PROJECT_ROOT / "host" / "tests" / "fixtures" / "events"
    / "valid-v3-loss-43.terp-event"
)

if str(TOOLS_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIRECTORY))

from check_memory_map import load_layout  # noqa: E402


def _message(
    name: str,
    message_id: int,
    response: tuple[str, ...],
    request: tuple[str, ...] | None = (),
) -> dict[str, object]:
    message: dict[str, object] = {"id": message_id, "name": name}
    if request is not None:
        message["request"] = list(request)
    message["response"] = list(response)
    return message


V1_MESSAGES = (
    _message(
        "HELLO",
        0x0001,
        (
            "protocol_version_u8",
            "maximum_payload_u32",
            "maximum_chunk_u32",
            "capabilities_u32",
        ),
        ("protocol_version_u8",),
    ),
    _message(
        "GET_DEVICE_INFO",
        0x0002,
        (
            "model_lp_utf8",
            "firmware_lp_utf8",
            "hardware_lp_utf8",
            "serial_lp_utf8",
            "capabilities_u32",
        ),
    ),
    _message(
        "GET_HEALTH",
        0x0003,
        (
            "state_u8",
            "storage_ready_u8",
            "free_log_bytes_u32",
            "storage_errors_u32",
            "export_errors_u32",
        ),
    ),
    _message("GET_TIME", 0x0004, ("utc_unix_seconds_i64", "epoch_id_u32")),
    _message(
        "SET_TIME",
        0x0005,
        ("utc_unix_seconds_i64", "epoch_id_u32"),
        ("utc_unix_seconds_i64",),
    ),
    _message(
        "LIST_EVENTS",
        0x0006,
        ("next_event_id_u32", "event_count_u16", "event_info_repeated"),
        ("after_event_id_u32", "maximum_count_u32"),
    ),
    _message(
        "GET_EVENT_INFO",
        0x0007,
        ("event_id_u32", "total_length_u32", "event_crc32_u32"),
        ("event_id_u32",),
    ),
    _message(
        "READ_EVENT_CHUNK",
        0x0008,
        (
            "event_id_u32",
            "actual_offset_u32",
            "total_length_u32",
            "actual_length_u32",
            "chunk_crc32_u32",
            "data_bytes",
        ),
        ("event_id_u32", "offset_u32", "requested_length_u32"),
    ),
    _message("DELETE_EVENT", 0x0009, ("empty",), ("event_id_u32",)),
    _message("START_LIVE", 0x000A, ("empty",)),
    _message("STOP_LIVE", 0x000B, ("empty",)),
    _message(
        "OTA_BEGIN",
        0x0100,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
        ("package_length_u32",),
    ),
    _message(
        "OTA_WRITE_CHUNK",
        0x0101,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
        ("offset_u32", "chunk_crc32_u32", "data_bytes"),
    ),
    _message(
        "OTA_QUERY",
        0x0102,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
    ),
    _message(
        "OTA_FINALIZE",
        0x0103,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
    ),
    _message("OTA_CANCEL", 0x0104, ("empty",)),
    _message(
        "OTA_RESULT",
        0x0105,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
        None,
    ),
    _message(
        "OTA_RECOVERY_BEGIN",
        0x0106,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
        ("package_length_u32",),
    ),
    _message(
        "OTA_RECOVERY_WRITE_CHUNK",
        0x0107,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
        ("offset_u32", "chunk_crc32_u32", "data_bytes"),
    ),
    _message(
        "OTA_RECOVERY_QUERY",
        0x0108,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
    ),
    _message(
        "OTA_RECOVERY_FINALIZE",
        0x0109,
        ("total_length_u32", "verified_length_u32", "pending_install_u8"),
    ),
    _message("OTA_RECOVERY_CANCEL", 0x010A, ("empty",)),
    _message(
        "MODEL_OTA_BEGIN",
        0x0110,
        (
            "total_length_u32",
            "verified_length_u32",
            "pending_install_u8",
            "active_slot_u8",
            "model_valid_u8",
        ),
        ("package_length_u32",),
    ),
    _message(
        "MODEL_OTA_WRITE_CHUNK",
        0x0111,
        (
            "total_length_u32",
            "verified_length_u32",
            "pending_install_u8",
            "active_slot_u8",
            "model_valid_u8",
        ),
        ("offset_u32", "chunk_crc32_u32", "data_bytes"),
    ),
    _message(
        "MODEL_OTA_QUERY",
        0x0112,
        (
            "total_length_u32",
            "verified_length_u32",
            "pending_install_u8",
            "active_slot_u8",
            "model_valid_u8",
        ),
    ),
    _message(
        "MODEL_OTA_FINALIZE",
        0x0113,
        (
            "total_length_u32",
            "verified_length_u32",
            "pending_install_u8",
            "active_slot_u8",
            "model_valid_u8",
        ),
    ),
    _message("MODEL_OTA_CANCEL", 0x0114, ("empty",)),
    _message(
        "GET_AI_RESULT",
        0x0200,
        (
            "event_id_u32",
            "model_version_u16",
            "status_u8",
            "class_index_u8",
            "class_count_u8",
            "quality_flags_u8",
            "event_flags_u16",
            "sample_count_u32",
            "model_crc32_u32",
            "confidence_f32",
            "logits_f32x4",
            "failure_reason_u16",
            "reserved_u16",
            "result_sequence_u32",
        ),
        ("event_id_u32",),
    ),
    _message(
        "ERROR",
        0xFFFF,
        ("error_code_u16", "request_message_type_u16"),
        None,
    ),
)

MEMORY_LAYOUT_V1 = {
    "flash_base": 0x08000000,
    "flash_size_bytes": 0x00200000,
    "bootloader_base": 0x08000000,
    "bootloader_size_bytes": 0x00020000,
    "application_base": 0x08020000,
    "application_size_bytes": 0x001A0000,
    "state_primary_base": 0x081C0000,
    "state_secondary_base": 0x081E0000,
    "state_record_size_bytes": 0x00020000,
    "qspi_size_bytes": 0x00800000,
    "qspi_page_size_bytes": 0x00000100,
    "qspi_erase_block_size_bytes": 0x00001000,
    "qspi_metadata_offset": 0x00000000,
    "qspi_metadata_size_bytes": 0x00010000,
    "qspi_model_state_offset": 0x00000000,
    "qspi_model_state_size_bytes": 0x00001000,
    "qspi_model_state_secondary_offset": 0x00507000,
    "qspi_model_state_secondary_size_bytes": 0x00001000,
    "qspi_ai_result_offset": 0x00001000,
    "qspi_ai_result_size_bytes": 0x0000E000,
    "qspi_hil_scratch_offset": 0x0000F000,
    "qspi_hil_scratch_size_bytes": 0x00001000,
    "qspi_candidate_offset": 0x00010000,
    "qspi_candidate_size_bytes": 0x00100000,
    "qspi_recovery_offset": 0x00110000,
    "qspi_recovery_size_bytes": 0x00100000,
    "qspi_model_a_offset": 0x00210000,
    "qspi_model_a_size_bytes": 0x002F8000,
    "qspi_model_b_offset": 0x00508000,
    "qspi_model_b_size_bytes": 0x002F8000,
    "package_max_image_bytes": 0x000FFF3C,
}

EXPECTED_QSPI_METADATA_SUBREGIONS = (
    ("qspi_model_state", 0x00000000, 0x00001000),
    ("qspi_ai_result", 0x00001000, 0x0000E000),
    ("qspi_hil_scratch", 0x0000F000, 0x00001000),
)

EV03_GOLDEN_SHA256 = (
    "77142ac8232c503af15c6e697e374e82a4828e855a8ac7c343db1f9bd1b43b2b"
)
EV03_SAMPLE = struct.Struct("<hhhhhhHbB")
EV03_SAMPLES = (
    (-7, 3, 1, 10, 11, 12, 100, 20, 0x68),
    (97, 4, 2, 13, 14, 15, 101, 21, 0x68),
    (-5, 5, 3, 16, 17, 18, 102, 22, 0x68),
    (6, 6, 4, 19, 20, 21, 103, 23, 0x68),
)
EV03_HEADER_FIELDS = (
    (0, "<4s", b"EV03"),
    (4, "<H", 3),
    (6, "<H", 160),
    (8, "<I", 43),
    (12, "<Q", 2_000_000),
    (20, "<q", 1_700_000_001),
    (28, "<I", 2),
    (32, "<I", 1600),
    (36, "<I", 101),
    (40, "<I", 1),
    (44, "<I", 3),
    (48, "<H", 0),
    (50, "<H", 8),
    (52, "<I", 9409),
    (56, "<I", 4096),
    (60, "<I", 64),
    (64, "<I", 0x28282DD9),
    (68, "<B", 0),
    (69, "<B", 0),
    (70, "<H", 0),
    (72, "<I", 0),
    (76, "<h", 0),
    (78, "<H", 0),
    (80, "<I", 0),
    (84, "<I", 0),
    (88, "<H", 0),
    (92, "<I", 0),
    (96, "<I", 0),
    (100, "<I", 0),
    (104, "<I", 0),
    (108, "<I", 0),
    (112, "<I", 0),
    (124, "<I", 0),
    (128, "<I", 6),
    (132, "<I", 102),
    (136, "<I", 109),
    (140, "<I", 2),
    (144, "<Q", 2_001_250),
    (152, "<Q", 2_005_625),
)


def _load_terp_document() -> dict[str, object]:
    return yaml.safe_load(TERP_MESSAGES_PATH.read_text(encoding="utf-8"))


def test_v1_release_tag_remains_the_immutable_rollback_baseline() -> None:
    completed = subprocess.run(
        ["git", "rev-parse", "v1.0.0^{}"],
        cwd=PROJECT_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )

    assert (
        completed.stdout.strip()
        == "a69b6c6c71b91e1267780c169760eca284b7523b"
    )


def test_existing_terp_v1_messages_preserve_contract() -> None:
    document = _load_terp_document()
    assert document["protocol"] == "TERP"
    assert document["version"] == 1
    assert document["byte_order"] == "little-endian"

    actual_messages = document["messages"]
    assert isinstance(actual_messages, list)
    assert len(
        {message["name"] for message in actual_messages}
    ) == len(actual_messages)
    assert len(
        {message["id"] for message in actual_messages}
    ) == len(actual_messages)

    expected_by_name = {message["name"]: message for message in V1_MESSAGES}
    actual_by_name = {message["name"]: message for message in actual_messages}
    assert set(expected_by_name).issubset(actual_by_name)

    expected_names = [message["name"] for message in V1_MESSAGES]
    actual_v1_names = [
        message["name"]
        for message in actual_messages
        if message["name"] in expected_by_name
    ]
    assert actual_v1_names == expected_names
    for name, expected in expected_by_name.items():
        assert actual_by_name[name] == expected


def test_generated_terp_registry_keeps_every_locked_v1_message_id() -> None:
    for message in V1_MESSAGES:
        assert MessageType[message["name"]].value == message["id"]


def test_internal_flash_and_qspi_layout_is_fully_frozen() -> None:
    layout = load_layout(MEMORY_LAYOUT_PATH)
    actual = {
        field.name: getattr(layout, field.name)
        for field in fields(layout)
    }

    assert actual == MEMORY_LAYOUT_V1
    assert layout.regions() == (
        ("bootloader", 0x08000000, 0x00020000),
        ("application", 0x08020000, 0x001A0000),
        ("state_primary", 0x081C0000, 0x00020000),
        ("state_secondary", 0x081E0000, 0x00020000),
    )
    assert layout.qspi_regions() == (
        ("qspi_metadata", 0x00000000, 0x00010000),
        ("qspi_candidate", 0x00010000, 0x00100000),
        ("qspi_recovery", 0x00110000, 0x00100000),
        ("qspi_model_a", 0x00210000, 0x002F8000),
        ("qspi_model_b", 0x00508000, 0x002F8000),
    )
    actual_metadata_subregions = (
        ("qspi_model_state", layout.qspi_model_state_offset,
         layout.qspi_model_state_size_bytes),
        ("qspi_ai_result", layout.qspi_ai_result_offset,
         layout.qspi_ai_result_size_bytes),
        ("qspi_hil_scratch", layout.qspi_hil_scratch_offset,
         layout.qspi_hil_scratch_size_bytes),
    )
    assert actual_metadata_subregions == EXPECTED_QSPI_METADATA_SUBREGIONS
    assert layout.application_end == layout.state_primary_base
    assert (
        layout.state_secondary_base + layout.state_record_size_bytes
        == layout.flash_end
    )
    assert (
        layout.qspi_model_state_secondary_offset
        + layout.qspi_model_state_secondary_size_bytes
        == layout.qspi_model_a_offset + layout.qspi_model_a_size_bytes
    )
    assert layout.package_max_image_bytes == 0x00100000 - 196


def test_ev03_golden_vector_freezes_layout_and_crc() -> None:
    raw = EV03_FIXTURE_PATH.read_bytes()
    payload = b"".join(EV03_SAMPLE.pack(*sample) for sample in EV03_SAMPLES)

    assert len(raw) == 224
    assert sha256(raw).hexdigest() == EV03_GOLDEN_SHA256
    assert raw[160:] == payload
    assert zlib.crc32(payload) & 0xFFFFFFFF == 0x28282DD9
    for offset, format_string, expected in EV03_HEADER_FIELDS:
        assert struct.unpack_from(format_string, raw, offset)[0] == expected
    assert raw[90:92] == bytes(2)
    assert raw[116:124] == bytes(8)

    decoded = parse_ev03(raw)
    loaded = load_event(EV03_FIXTURE_PATH)
    assert decoded.event_id == loaded.metadata.event_id == 43
    assert decoded.lost_sample_count == loaded.metadata.lost_sample_count == 6
    assert loaded.sample_count == len(EV03_SAMPLES)


@pytest.mark.parametrize(
    ("offset", "replacement"),
    (
        (0, b"BAD!"),
        (4, struct.pack("<H", 4)),
        (6, struct.pack("<H", 128)),
        (40, struct.pack("<I", 2)),
        (64, struct.pack("<I", 0)),
        (160, b"\x00"),
    ),
)
def test_ev03_readers_reject_header_sample_and_crc_contract_mutations(
    tmp_path: Path, offset: int, replacement: bytes
) -> None:
    mutated = bytearray(EV03_FIXTURE_PATH.read_bytes())
    mutated[offset:offset + len(replacement)] = replacement
    mutated_path = tmp_path / "mutated.terp-event"
    mutated_path.write_bytes(mutated)

    with pytest.raises(EventFormatError):
        parse_ev03(bytes(mutated))
    with pytest.raises(ValueError):
        load_event(mutated_path)


def test_old_host_parses_legal_non_pass_ev03_without_inventing_verdict(
    tmp_path: Path,
) -> None:
    cases = (
        ("short", 1, True),
        ("capped", 2, True),
        ("sequence_gap", 8, False),
    )

    for name, flags, clear_loss_summary in cases:
        raw = bytearray(EV03_FIXTURE_PATH.read_bytes())
        struct.pack_into("<H", raw, 50, flags)
        if clear_loss_summary:
            raw[128:160] = bytes(32)
        source = tmp_path / f"{name}.terp-event"
        source.write_bytes(raw)

        parsed = parse_ev03(bytes(raw))
        loaded = load_event(source)
        repository = EventRepository(tmp_path / f"repository-{name}")
        stored = repository.import_event(source, device_serial="OLD-HOST")

        assert parsed.flags == flags
        assert loaded.metadata.flags == flags
        assert stored.metadata.flags == flags
        assert stored.ai_result is None
        assert not hasattr(parsed, "verdict")
        assert not hasattr(loaded, "verdict")
        assert repository.list_events() == [stored]
