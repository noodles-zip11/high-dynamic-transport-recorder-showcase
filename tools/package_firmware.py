"""Create deterministic, integrity-checked Phase 11 firmware packages."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
import re
import struct
import sys
import zlib

from check_memory_map import (
    FIRMWARE_MANIFEST_FORMAT,
    LayoutError,
    MemoryLayout,
    load_layout,
    validate_layout,
)


class PackageError(ValueError):
    """Raised when a package field cannot be represented safely."""


MAGIC = b"TRFW"
FORMAT_VERSION = 1
IDENTIFIER_BYTES = 32
RESERVED_BYTES = 16
MANIFEST_STRUCT = struct.Struct(FIRMWARE_MANIFEST_FORMAT)
MANIFEST_HEADER_BYTES = MANIFEST_STRUCT.size
BOOTLOADER_VERSION_PATTERN = re.compile(
    r"^(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})$"
)


@dataclass(frozen=True)
class FirmwareManifest:
    product_id: str
    hardware_id: str
    firmware_version: str
    minimum_bootloader_version: str
    target_address: int
    image_length: int
    image_crc32: int
    image_sha256: bytes


def _encode_identifier(field_name: str, value: str) -> bytes:
    if not isinstance(value, str) or not value.strip():
        raise PackageError(f"{field_name} must not be empty")
    if "\x00" in value:
        raise PackageError(f"{field_name} must not contain NUL")
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise PackageError(f"{field_name} must contain ASCII text") from error
    if len(encoded) > IDENTIFIER_BYTES:
        raise PackageError(f"{field_name} exceeds {IDENTIFIER_BYTES} bytes")
    return encoded.ljust(IDENTIFIER_BYTES, b"\x00")


def is_valid_bootloader_version(value: str) -> bool:
    """Return whether a version has the same grammar accepted by the Bootloader."""
    match = BOOTLOADER_VERSION_PATTERN.fullmatch(value) if isinstance(value, str) else None
    return match is not None and all(int(component) <= 255 for component in value.split("."))


def build_package(
    *,
    image: bytes,
    product_id: str,
    hardware_id: str,
    firmware_version: str,
    minimum_bootloader_version: str,
    target_address: int,
    layout: MemoryLayout,
) -> bytes:
    """Build bytes whose header is fixed-width and always little-endian."""
    layout = validate_layout(layout)
    if not isinstance(image, bytes):
        raise PackageError("image must be bytes")
    if not image:
        raise PackageError("image must not be empty")
    if target_address != layout.application_base:
        raise PackageError("target_address must equal the application base")
    if len(image) > layout.application_size_bytes:
        raise PackageError("image does not fit the application region")
    if len(image) + MANIFEST_HEADER_BYTES > layout.qspi_candidate_size_bytes:
        raise PackageError("image plus manifest does not fit the candidate package slot")
    if len(image) > layout.package_max_image_bytes:
        raise PackageError("image exceeds the configured package maximum")
    if not is_valid_bootloader_version(minimum_bootloader_version):
        raise PackageError("minimum_bootloader_version must use major.minor.patch with 0..255 values")

    image_crc32 = zlib.crc32(image) & 0xFFFFFFFF
    image_sha256 = sha256(image).digest()
    manifest = FirmwareManifest(
        product_id=product_id,
        hardware_id=hardware_id,
        firmware_version=firmware_version,
        minimum_bootloader_version=minimum_bootloader_version,
        target_address=target_address,
        image_length=len(image),
        image_crc32=image_crc32,
        image_sha256=image_sha256,
    )
    header = MANIFEST_STRUCT.pack(
        MAGIC,
        FORMAT_VERSION,
        MANIFEST_HEADER_BYTES,
        _encode_identifier("product_id", manifest.product_id),
        _encode_identifier("hardware_id", manifest.hardware_id),
        _encode_identifier("firmware_version", manifest.firmware_version),
        _encode_identifier("minimum_bootloader_version", manifest.minimum_bootloader_version),
        manifest.target_address,
        manifest.image_length,
        manifest.image_crc32,
        manifest.image_sha256,
        bytes(RESERVED_BYTES),
    )
    return header + image


def _parse_integer(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer such as 0x08020000") from error


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a deterministic Phase 11 firmware package.")
    parser.add_argument("--input", required=True, type=Path, help="raw application image")
    parser.add_argument("--output", required=True, type=Path, help="package destination")
    parser.add_argument("--layout", required=True, type=Path, help="checked memory layout")
    parser.add_argument("--product-id", required=True)
    parser.add_argument("--hardware-id", required=True)
    parser.add_argument("--firmware-version", required=True)
    parser.add_argument("--minimum-bootloader-version", required=True)
    parser.add_argument("--target", required=True, type=_parse_integer)
    arguments = parser.parse_args()

    try:
        package = build_package(
            image=arguments.input.read_bytes(),
            product_id=arguments.product_id,
            hardware_id=arguments.hardware_id,
            firmware_version=arguments.firmware_version,
            minimum_bootloader_version=arguments.minimum_bootloader_version,
            target_address=arguments.target,
            layout=load_layout(arguments.layout),
        )
        arguments.output.write_bytes(package)
    except (LayoutError, PackageError, OSError) as error:
        print(f"firmware package error: {error}", file=sys.stderr)
        return 2
    print(f"wrote {arguments.output} ({len(package)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
