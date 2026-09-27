"""Offline checks for the Phase 11 memory map and firmware package tools."""

from __future__ import annotations

import importlib
from hashlib import sha256
from pathlib import Path
import struct
import subprocess
import sys
import zlib

import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIRECTORY = PROJECT_ROOT / "tools"
LAYOUT_PATH = PROJECT_ROOT / "config" / "memory_layout.yaml"
HEADER_PATH = PROJECT_ROOT / "firmware" / "config" / "memory_layout.h"
CHECK_MEMORY_MAP_PATH = TOOLS_DIRECTORY / "check_memory_map.py"
PACKAGE_FIRMWARE_PATH = TOOLS_DIRECTORY / "package_firmware.py"
INSPECT_PACKAGE_PATH = TOOLS_DIRECTORY / "inspect_package.py"

if str(TOOLS_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIRECTORY))


def _tools():
    return (
        importlib.import_module("check_memory_map"),
        importlib.import_module("inspect_package"),
        importlib.import_module("package_firmware"),
    )


def _package(
    image: bytes = b"phase-11-firmware-image", minimum_bootloader_version: str = "1.0.0"
) -> bytes:
    check_memory_map, _inspect_package, package_firmware = _tools()
    layout = check_memory_map.load_layout(LAYOUT_PATH)
    return package_firmware.build_package(
        image=image,
        product_id="transport-recorder",
        hardware_id="stm32h743vit6",
        firmware_version="1.2.3",
        minimum_bootloader_version=minimum_bootloader_version,
        target_address=layout.application_base,
        layout=layout,
    )


def _replace_manifest_field(package: bytes, field_index: int, value: object) -> bytes:
    _check_memory_map, _inspect_package, package_firmware = _tools()
    fields = list(package_firmware.MANIFEST_STRUCT.unpack_from(package))
    fields[field_index] = value
    mutated = bytearray(package)
    mutated[:package_firmware.MANIFEST_HEADER_BYTES] = package_firmware.MANIFEST_STRUCT.pack(
        *fields
    )
    return bytes(mutated)


def _run_tool(path: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(path), *arguments],
        cwd=PROJECT_ROOT,
        capture_output=True,
        check=False,
        text=True,
    )


def test_layout_rejects_overlapping_regions(tmp_path: Path) -> None:
    check_memory_map, _inspect_package, _package_firmware = _tools()
    overlapping = tmp_path / "overlapping.yaml"
    overlapping.write_text(
        LAYOUT_PATH.read_text(encoding="utf-8").replace(
            "application_base: 0x08020000", "application_base: 0x08010000"
        ),
        encoding="utf-8",
    )

    with pytest.raises(check_memory_map.LayoutError, match="overlaps"):
        check_memory_map.load_layout(overlapping)


def test_layout_rejects_an_application_region_that_overflows_into_state_records(
    tmp_path: Path,
) -> None:
    check_memory_map, _inspect_package, _package_firmware = _tools()
    overflowing = tmp_path / "overflowing.yaml"
    overflowing.write_text(
        LAYOUT_PATH.read_text(encoding="utf-8").replace(
            "application_size_bytes: 0x001A0000", "application_size_bytes: 0x001B0000"
        ),
        encoding="utf-8",
    )

    with pytest.raises(check_memory_map.LayoutError, match="application.*overlaps"):
        check_memory_map.load_layout(overflowing)


def test_layout_requires_each_locked_qspi_slot(tmp_path: Path) -> None:
    check_memory_map, _inspect_package, _package_firmware = _tools()
    incomplete = tmp_path / "incomplete-qspi.yaml"
    incomplete.write_text(
        "\n".join(
            line
            for line in LAYOUT_PATH.read_text(encoding="utf-8").splitlines()
            if not line.startswith("qspi_model_b_offset:")
        ),
        encoding="utf-8",
    )

    with pytest.raises(check_memory_map.LayoutError, match="qspi_model_b_offset"):
        check_memory_map.load_layout(incomplete)


def test_layout_rejects_overlapping_locked_qspi_slots(tmp_path: Path) -> None:
    check_memory_map, _inspect_package, _package_firmware = _tools()
    overlapping = tmp_path / "overlapping-qspi.yaml"
    overlapping.write_text(
        LAYOUT_PATH.read_text(encoding="utf-8").replace(
            "qspi_candidate_offset: 0x00010000", "qspi_candidate_offset: 0x00008000"
        ),
        encoding="utf-8",
    )

    with pytest.raises(check_memory_map.LayoutError, match="qspi_metadata.*qspi_candidate"):
        check_memory_map.load_layout(overlapping)


def test_layout_freezes_the_phase11_application_capacity() -> None:
    check_memory_map, _inspect_package, _package_firmware = _tools()
    layout = check_memory_map.load_layout(LAYOUT_PATH)

    assert layout.application_base == 0x08020000
    assert layout.application_size_bytes == 0x001A0000
    assert layout.application_end == 0x081C0000
    assert layout.state_primary_base == 0x081C0000
    assert layout.state_secondary_base == 0x081E0000
    assert layout.state_record_size_bytes == 0x00020000


def test_layout_rejects_a_state_record_smaller_than_one_h743_erase_sector(
    tmp_path: Path,
) -> None:
    check_memory_map, _inspect_package, _package_firmware = _tools()
    unsafe = tmp_path / "unsafe-state-size.yaml"
    unsafe.write_text(
        LAYOUT_PATH.read_text(encoding="utf-8").replace(
            "state_record_size_bytes: 0x00020000", "state_record_size_bytes: 0x00010000"
        ),
        encoding="utf-8",
    )

    with pytest.raises(check_memory_map.LayoutError, match="128 KiB H743 Flash sector"):
        check_memory_map.load_layout(unsafe)


def test_layout_freezes_u3_w25q64_geometry_and_slots() -> None:
    check_memory_map, _inspect_package, _package_firmware = _tools()
    layout = check_memory_map.load_layout(LAYOUT_PATH)

    assert layout.qspi_size_bytes == 0x00800000
    assert layout.qspi_page_size_bytes == 0x00000100
    assert layout.qspi_erase_block_size_bytes == 0x00001000
    assert layout.qspi_regions() == (
        ("qspi_metadata", 0x00000000, 0x00010000),
        ("qspi_candidate", 0x00010000, 0x00100000),
        ("qspi_recovery", 0x00110000, 0x00100000),
        ("qspi_model_a", 0x00210000, 0x002F8000),
        ("qspi_model_b", 0x00508000, 0x002F8000),
    )


def test_layout_package_limit_accounts_for_the_actual_manifest_header() -> None:
    check_memory_map, _inspect_package, package_firmware = _tools()
    layout = check_memory_map.load_layout(LAYOUT_PATH)

    assert package_firmware.MANIFEST_HEADER_BYTES == 196
    assert layout.package_max_image_bytes == (
        layout.qspi_candidate_size_bytes - package_firmware.MANIFEST_HEADER_BYTES
    )
    assert layout.package_max_image_bytes == 0x000FFF3C


def test_layout_header_mirrors_the_checked_layout() -> None:
    _check_memory_map, _inspect_package, _package_firmware = _tools()
    header = HEADER_PATH.read_text(encoding="utf-8")

    assert "TRANSPORT_OTA_BOOTLOADER_BASE UINT32_C(0x08000000)" in header
    assert "TRANSPORT_OTA_APPLICATION_BASE UINT32_C(0x08020000)" in header
    assert "TRANSPORT_OTA_APPLICATION_SIZE_BYTES UINT32_C(0x001A0000)" in header
    assert "TRANSPORT_OTA_STATE_PRIMARY_BASE UINT32_C(0x081C0000)" in header
    assert "TRANSPORT_OTA_STATE_SECONDARY_BASE UINT32_C(0x081E0000)" in header
    assert "TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES UINT32_C(0x00020000)" in header
    assert "TRANSPORT_OTA_QSPI_SIZE_BYTES UINT32_C(0x00800000)" in header
    assert "TRANSPORT_OTA_QSPI_PAGE_SIZE_BYTES UINT32_C(0x00000100)" in header
    assert "TRANSPORT_OTA_QSPI_ERASE_BLOCK_SIZE_BYTES UINT32_C(0x00001000)" in header
    assert "TRANSPORT_OTA_QSPI_METADATA_OFFSET UINT32_C(0x00000000)" in header
    assert "TRANSPORT_OTA_QSPI_METADATA_SIZE_BYTES UINT32_C(0x00010000)" in header
    assert "TRANSPORT_OTA_QSPI_MODEL_STATE_OFFSET UINT32_C(0x00000000)" in header
    assert "TRANSPORT_OTA_QSPI_MODEL_STATE_SIZE_BYTES UINT32_C(0x00001000)" in header
    assert "TRANSPORT_OTA_QSPI_AI_RESULT_OFFSET UINT32_C(0x00001000)" in header
    assert "TRANSPORT_OTA_QSPI_AI_RESULT_SIZE_BYTES UINT32_C(0x0000E000)" in header
    assert "TRANSPORT_OTA_QSPI_HIL_SCRATCH_OFFSET UINT32_C(0x0000F000)" in header
    assert "TRANSPORT_OTA_QSPI_HIL_SCRATCH_SIZE_BYTES UINT32_C(0x00001000)" in header
    assert "TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET UINT32_C(0x00010000)" in header
    assert "TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES UINT32_C(0x00100000)" in header
    assert "TRANSPORT_OTA_QSPI_RECOVERY_OFFSET UINT32_C(0x00110000)" in header
    assert "TRANSPORT_OTA_QSPI_RECOVERY_SIZE_BYTES UINT32_C(0x00100000)" in header
    assert "TRANSPORT_OTA_QSPI_MODEL_A_OFFSET UINT32_C(0x00210000)" in header
    assert "TRANSPORT_OTA_QSPI_MODEL_A_SIZE_BYTES UINT32_C(0x002F8000)" in header
    assert "TRANSPORT_OTA_QSPI_MODEL_B_OFFSET UINT32_C(0x00508000)" in header
    assert "TRANSPORT_OTA_QSPI_MODEL_B_SIZE_BYTES UINT32_C(0x002F8000)" in header
    assert "TRANSPORT_OTA_PACKAGE_MAX_IMAGE_BYTES UINT32_C(0x000FFF3C)" in header


def test_builder_rejects_image_above_the_phase11_application_capacity() -> None:
    _check_memory_map, _inspect_package, package_firmware = _tools()

    with pytest.raises(package_firmware.PackageError, match="application region"):
        _package(b"\xA5" * (0x001A0000 + 1))


def test_builder_and_inspector_reject_an_empty_firmware_image() -> None:
    check_memory_map, inspect_package, package_firmware = _tools()
    layout = check_memory_map.load_layout(LAYOUT_PATH)

    with pytest.raises(package_firmware.PackageError, match="must not be empty"):
        _package(b"")

    empty_package = package_firmware.MANIFEST_STRUCT.pack(
        package_firmware.MAGIC,
        package_firmware.FORMAT_VERSION,
        package_firmware.MANIFEST_HEADER_BYTES,
        b"transport-recorder".ljust(32, b"\x00"),
        b"stm32h743vit6".ljust(32, b"\x00"),
        b"1.2.3".ljust(32, b"\x00"),
        b"1.0.0".ljust(32, b"\x00"),
        layout.application_base,
        0,
        0,
        sha256(b"").digest(),
        bytes(package_firmware.RESERVED_BYTES),
    )
    with pytest.raises(inspect_package.PackageInspectionError, match="must not be empty"):
        inspect_package.inspect_package(empty_package, layout=layout)


def test_builder_accounts_for_the_manifest_inside_the_qspi_candidate_slot() -> None:
    check_memory_map, _inspect_package, package_firmware = _tools()
    layout = check_memory_map.load_layout(LAYOUT_PATH)
    maximum_image_bytes = layout.qspi_candidate_size_bytes - package_firmware.MANIFEST_HEADER_BYTES

    package = _package(b"\xA5" * maximum_image_bytes)
    assert len(package) == layout.qspi_candidate_size_bytes

    with pytest.raises(package_firmware.PackageError, match="candidate package slot"):
        _package(b"\xA5" * (maximum_image_bytes + 1))


@pytest.mark.parametrize(
    "field_name",
    ("product_id", "hardware_id", "firmware_version", "minimum_bootloader_version"),
)
def test_builder_rejects_empty_required_identity_fields(field_name: str) -> None:
    check_memory_map, _inspect_package, package_firmware = _tools()
    layout = check_memory_map.load_layout(LAYOUT_PATH)
    fields = {
        "product_id": "transport-recorder",
        "hardware_id": "stm32h743vit6",
        "firmware_version": "1.2.3",
        "minimum_bootloader_version": "1.0.0",
    }
    fields[field_name] = ""

    with pytest.raises(package_firmware.PackageError, match=field_name):
        package_firmware.build_package(
            image=b"image",
            target_address=layout.application_base,
            layout=layout,
            **fields,
        )


@pytest.mark.parametrize(
    "minimum_bootloader_version",
    ("1", "1.2", "1.2.3.4", "01.2.3", "1.-1.3", "256.0.0"),
)
def test_builder_rejects_minimum_bootloader_versions_the_bootloader_cannot_parse(
    minimum_bootloader_version: str,
) -> None:
    _check_memory_map, _inspect_package, package_firmware = _tools()

    with pytest.raises(package_firmware.PackageError, match="minimum_bootloader_version"):
        _package(minimum_bootloader_version=minimum_bootloader_version)


@pytest.mark.parametrize(
    "minimum_bootloader_version",
    ("1", "1.2", "1.2.3.4", "01.2.3", "1.-1.3", "256.0.0"),
)
def test_inspector_rejects_minimum_bootloader_versions_the_bootloader_cannot_parse(
    minimum_bootloader_version: str,
) -> None:
    check_memory_map, inspect_package, _package_firmware = _tools()
    tampered = _replace_manifest_field(
        _package(), 6, minimum_bootloader_version.encode("ascii").ljust(32, b"\x00")
    )

    with pytest.raises(inspect_package.PackageInspectionError, match="minimum_bootloader_version"):
        inspect_package.inspect_package(tampered, layout=check_memory_map.load_layout(LAYOUT_PATH))


def test_package_header_is_little_endian_and_deterministic() -> None:
    check_memory_map, inspect_package, package_firmware = _tools()
    first = _package()
    second = _package()

    assert first == second
    assert package_firmware.MANIFEST_STRUCT.format == "<4sHH32s32s32s32sIII32s16s"
    assert len(first) == package_firmware.MANIFEST_HEADER_BYTES + len(b"phase-11-firmware-image")

    inspected = inspect_package.inspect_package(
        first,
        layout=check_memory_map.load_layout(LAYOUT_PATH),
    )
    assert inspected.product_id == "transport-recorder"
    assert inspected.hardware_id == "stm32h743vit6"
    assert inspected.firmware_version == "1.2.3"
    assert inspected.minimum_bootloader_version == "1.0.0"
    assert inspected.target_address == 0x08020000
    assert inspected.image == b"phase-11-firmware-image"


def test_inspector_rejects_altered_manifest_crc32() -> None:
    check_memory_map, inspect_package, _package_firmware = _tools()
    tampered = _replace_manifest_field(_package(), 9, 0)

    with pytest.raises(inspect_package.PackageInspectionError, match="CRC32"):
        inspect_package.inspect_package(tampered, layout=check_memory_map.load_layout(LAYOUT_PATH))


def test_inspector_rejects_altered_manifest_sha256() -> None:
    check_memory_map, inspect_package, _package_firmware = _tools()
    tampered = _replace_manifest_field(_package(), 10, bytes(32))

    with pytest.raises(inspect_package.PackageInspectionError, match="SHA-256"):
        inspect_package.inspect_package(tampered, layout=check_memory_map.load_layout(LAYOUT_PATH))


def test_inspector_rejects_nonzero_reserved_manifest_bytes() -> None:
    check_memory_map, inspect_package, _package_firmware = _tools()
    tampered = _replace_manifest_field(_package(), 11, b"\x01" + bytes(15))

    with pytest.raises(inspect_package.PackageInspectionError, match="reserved"):
        inspect_package.inspect_package(tampered, layout=check_memory_map.load_layout(LAYOUT_PATH))


def test_inspector_rejects_a_correctly_checksummed_oversize_candidate_package() -> None:
    check_memory_map, inspect_package, package_firmware = _tools()
    layout = check_memory_map.load_layout(LAYOUT_PATH)
    image = b"\xA5" * (
        layout.qspi_candidate_size_bytes - package_firmware.MANIFEST_HEADER_BYTES + 1
    )
    fields = list(package_firmware.MANIFEST_STRUCT.unpack_from(_package()))
    fields[8] = len(image)
    fields[9] = zlib.crc32(image) & 0xFFFFFFFF
    fields[10] = sha256(image).digest()
    oversized = package_firmware.MANIFEST_STRUCT.pack(*fields) + image

    with pytest.raises(inspect_package.PackageInspectionError, match="candidate package slot"):
        inspect_package.inspect_package(oversized, layout=layout)


@pytest.mark.parametrize(
    ("field_name", "field_index"),
    (
        ("product_id", 3),
        ("hardware_id", 4),
        ("firmware_version", 5),
        ("minimum_bootloader_version", 6),
    ),
)
def test_inspector_rejects_whitespace_only_identity_fields(
    field_name: str,
    field_index: int,
) -> None:
    check_memory_map, inspect_package, _package_firmware = _tools()
    tampered = _replace_manifest_field(_package(), field_index, b"   ".ljust(32, b"\x00"))

    with pytest.raises(inspect_package.PackageInspectionError, match=field_name):
        inspect_package.inspect_package(tampered, layout=check_memory_map.load_layout(LAYOUT_PATH))


def test_inspector_rejects_a_payload_crc32_mismatch() -> None:
    check_memory_map, inspect_package, package_firmware = _tools()
    tampered = bytearray(_package())
    tampered[package_firmware.MANIFEST_HEADER_BYTES] ^= 0x01

    with pytest.raises(inspect_package.PackageInspectionError, match="CRC32"):
        inspect_package.inspect_package(
            bytes(tampered),
            layout=check_memory_map.load_layout(LAYOUT_PATH),
        )


def test_memory_map_cli_reports_layout_errors_without_a_traceback(tmp_path: Path) -> None:
    invalid_layout = tmp_path / "invalid-layout.yaml"
    invalid_layout.write_text(
        LAYOUT_PATH.read_text(encoding="utf-8").replace(
            "qspi_page_size_bytes: 0x00000100", "qspi_page_size_bytes: 0x00000200"
        ),
        encoding="utf-8",
    )

    result = _run_tool(CHECK_MEMORY_MAP_PATH, str(invalid_layout))

    assert result.returncode == 2
    assert result.stdout == ""
    assert result.stderr.startswith("memory layout error:")
    assert "Traceback" not in result.stderr


def test_package_cli_reports_package_errors_without_a_traceback(tmp_path: Path) -> None:
    image_path = tmp_path / "image.bin"
    package_path = tmp_path / "image.pkg"
    image_path.write_bytes(b"image")

    result = _run_tool(
        PACKAGE_FIRMWARE_PATH,
        "--input",
        str(image_path),
        "--output",
        str(package_path),
        "--layout",
        str(LAYOUT_PATH),
        "--product-id",
        "",
        "--hardware-id",
        "stm32h743vit6",
        "--firmware-version",
        "1.2.3",
        "--minimum-bootloader-version",
        "1.0.0",
        "--target",
        "0x08020000",
    )

    assert result.returncode == 2
    assert result.stdout == ""
    assert result.stderr.startswith("firmware package error: product_id")
    assert "Traceback" not in result.stderr


def test_inspect_cli_reports_package_errors_without_a_traceback(tmp_path: Path) -> None:
    package_path = tmp_path / "invalid.pkg"
    invalid = bytearray(_package())
    invalid[-1] ^= 0x01
    package_path.write_bytes(invalid)

    result = _run_tool(INSPECT_PACKAGE_PATH, str(package_path), "--layout", str(LAYOUT_PATH))

    assert result.returncode == 2
    assert result.stdout == ""
    assert result.stderr.startswith("firmware package inspection error: image CRC32 mismatch")
    assert "Traceback" not in result.stderr
