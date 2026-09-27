"""Inspect and verify a deterministic Phase 11 firmware package."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import sys
import zlib

from package_firmware import (
    FORMAT_VERSION,
    MAGIC,
    MANIFEST_HEADER_BYTES,
    MANIFEST_STRUCT,
    RESERVED_BYTES,
    is_valid_bootloader_version,
)
from check_memory_map import LayoutError, MemoryLayout, load_layout, validate_layout


class PackageInspectionError(ValueError):
    """Raised when a package is malformed or its integrity checks fail."""


@dataclass(frozen=True)
class InspectedPackage:
    product_id: str
    hardware_id: str
    firmware_version: str
    minimum_bootloader_version: str
    target_address: int
    image_length: int
    image_crc32: int
    image_sha256: bytes
    image: bytes


def _decode_identifier(field_name: str, value: bytes) -> str:
    text_bytes = value.rstrip(b"\x00")
    if not text_bytes or b"\x00" in text_bytes:
        raise PackageInspectionError(f"{field_name} is empty or contains an embedded NUL")
    try:
        text = text_bytes.decode("ascii")
    except UnicodeDecodeError as error:
        raise PackageInspectionError(f"{field_name} is not ASCII") from error
    if not text.strip():
        raise PackageInspectionError(f"{field_name} must not be whitespace only")
    return text


def inspect_package(package: bytes, *, layout: MemoryLayout) -> InspectedPackage:
    """Parse and verify a package before a bootloader ever considers it."""
    try:
        layout = validate_layout(layout)
    except ValueError as error:
        raise PackageInspectionError(f"invalid package layout: {error}") from error
    if len(package) < MANIFEST_HEADER_BYTES:
        raise PackageInspectionError("package is shorter than the manifest header")
    (
        magic,
        format_version,
        header_bytes,
        product_id,
        hardware_id,
        firmware_version,
        minimum_bootloader_version,
        target_address,
        image_length,
        image_crc32,
        image_sha256,
        reserved,
    ) = MANIFEST_STRUCT.unpack_from(package)
    if magic != MAGIC:
        raise PackageInspectionError("manifest magic mismatch")
    if format_version != FORMAT_VERSION:
        raise PackageInspectionError("unsupported manifest format version")
    if header_bytes != MANIFEST_HEADER_BYTES:
        raise PackageInspectionError("manifest header size mismatch")
    if reserved != bytes(RESERVED_BYTES):
        raise PackageInspectionError("manifest reserved bytes must be zero")
    if len(package) != header_bytes + image_length:
        raise PackageInspectionError("package length does not match manifest image length")
    if image_length == 0:
        raise PackageInspectionError("package image must not be empty")
    if target_address != layout.application_base:
        raise PackageInspectionError("package target does not match the application base")
    if image_length > layout.application_size_bytes:
        raise PackageInspectionError("image does not fit the application region")
    if header_bytes + image_length > layout.qspi_candidate_size_bytes:
        raise PackageInspectionError("image plus manifest does not fit the candidate package slot")
    if image_length > layout.package_max_image_bytes:
        raise PackageInspectionError("image exceeds the configured package maximum")

    image = package[header_bytes:]
    actual_crc32 = zlib.crc32(image) & 0xFFFFFFFF
    if actual_crc32 != image_crc32:
        raise PackageInspectionError("image CRC32 mismatch")
    actual_sha256 = sha256(image).digest()
    if actual_sha256 != image_sha256:
        raise PackageInspectionError("image SHA-256 mismatch")
    decoded_minimum_bootloader_version = _decode_identifier(
        "minimum_bootloader_version", minimum_bootloader_version
    )
    if not is_valid_bootloader_version(decoded_minimum_bootloader_version):
        raise PackageInspectionError(
            "minimum_bootloader_version must use major.minor.patch with 0..255 values"
        )
    return InspectedPackage(
        product_id=_decode_identifier("product_id", product_id),
        hardware_id=_decode_identifier("hardware_id", hardware_id),
        firmware_version=_decode_identifier("firmware_version", firmware_version),
        minimum_bootloader_version=decoded_minimum_bootloader_version,
        target_address=target_address,
        image_length=image_length,
        image_crc32=image_crc32,
        image_sha256=image_sha256,
        image=image,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify and describe a Phase 11 firmware package.")
    parser.add_argument("package", type=Path, help="firmware package to inspect")
    parser.add_argument("--layout", required=True, type=Path, help="checked memory layout")
    arguments = parser.parse_args()
    try:
        inspected = inspect_package(
            arguments.package.read_bytes(),
            layout=load_layout(arguments.layout),
        )
    except (LayoutError, PackageInspectionError, OSError) as error:
        print(f"firmware package inspection error: {error}", file=sys.stderr)
        return 2
    print(
        json.dumps(
            {
                "firmware_version": inspected.firmware_version,
                "hardware_id": inspected.hardware_id,
                "image_crc32": f"0x{inspected.image_crc32:08X}",
                "image_length": inspected.image_length,
                "image_sha256": inspected.image_sha256.hex(),
                "minimum_bootloader_version": inspected.minimum_bootloader_version,
                "product_id": inspected.product_id,
                "target_address": f"0x{inspected.target_address:08X}",
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
