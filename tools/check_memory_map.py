"""Check the intentionally small Phase 11 internal-Flash layout format."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import struct
import sys
from typing import Iterable


class LayoutError(ValueError):
    """Raised when a memory layout cannot safely describe the device Flash."""


_KEY_VALUE_PATTERN = re.compile(r"(?P<key>[a-z][a-z0-9_]*)\s*:\s*(?P<value>\S+)\Z")
_W25Q64_SIZE_BYTES = 0x00800000
_W25Q64_PAGE_SIZE_BYTES = 0x00000100
_W25Q64_ERASE_BLOCK_SIZE_BYTES = 0x00001000
_H743_STATE_SECTOR_BYTES = 0x00020000
FIRMWARE_MANIFEST_FORMAT = "<4sHH32s32s32s32sIII32s16s"
FIRMWARE_MANIFEST_HEADER_BYTES = struct.calcsize(FIRMWARE_MANIFEST_FORMAT)
_LOCKED_QSPI_REGIONS = (
    ("qspi_metadata", 0x00000000, 0x00010000),
    ("qspi_candidate", 0x00010000, 0x00100000),
    ("qspi_recovery", 0x00110000, 0x00100000),
    ("qspi_model_a", 0x00210000, 0x002F8000),
    ("qspi_model_b", 0x00508000, 0x002F8000),
)
_REQUIRED_KEYS = frozenset(
    {
        "flash_base",
        "flash_size_bytes",
        "bootloader_base",
        "bootloader_size_bytes",
        "application_base",
        "application_size_bytes",
        "state_primary_base",
        "state_secondary_base",
        "state_record_size_bytes",
        "qspi_size_bytes",
        "qspi_page_size_bytes",
        "qspi_erase_block_size_bytes",
        "qspi_metadata_offset",
        "qspi_metadata_size_bytes",
        "qspi_model_state_offset",
        "qspi_model_state_size_bytes",
        "qspi_model_state_secondary_offset",
        "qspi_model_state_secondary_size_bytes",
        "qspi_ai_result_offset",
        "qspi_ai_result_size_bytes",
        "qspi_hil_scratch_offset",
        "qspi_hil_scratch_size_bytes",
        "qspi_candidate_offset",
        "qspi_candidate_size_bytes",
        "qspi_recovery_offset",
        "qspi_recovery_size_bytes",
        "qspi_model_a_offset",
        "qspi_model_a_size_bytes",
        "qspi_model_b_offset",
        "qspi_model_b_size_bytes",
        "package_max_image_bytes",
    }
)


@dataclass(frozen=True)
class MemoryLayout:
    flash_base: int
    flash_size_bytes: int
    bootloader_base: int
    bootloader_size_bytes: int
    application_base: int
    application_size_bytes: int
    state_primary_base: int
    state_secondary_base: int
    state_record_size_bytes: int
    qspi_size_bytes: int
    qspi_page_size_bytes: int
    qspi_erase_block_size_bytes: int
    qspi_metadata_offset: int
    qspi_metadata_size_bytes: int
    qspi_model_state_offset: int
    qspi_model_state_size_bytes: int
    qspi_model_state_secondary_offset: int
    qspi_model_state_secondary_size_bytes: int
    qspi_ai_result_offset: int
    qspi_ai_result_size_bytes: int
    qspi_hil_scratch_offset: int
    qspi_hil_scratch_size_bytes: int
    qspi_candidate_offset: int
    qspi_candidate_size_bytes: int
    qspi_recovery_offset: int
    qspi_recovery_size_bytes: int
    qspi_model_a_offset: int
    qspi_model_a_size_bytes: int
    qspi_model_b_offset: int
    qspi_model_b_size_bytes: int
    package_max_image_bytes: int

    @property
    def flash_end(self) -> int:
        return self.flash_base + self.flash_size_bytes

    @property
    def application_end(self) -> int:
        return self.application_base + self.application_size_bytes

    def regions(self) -> Iterable[tuple[str, int, int]]:
        return (
            ("bootloader", self.bootloader_base, self.bootloader_size_bytes),
            ("application", self.application_base, self.application_size_bytes),
            ("state_primary", self.state_primary_base, self.state_record_size_bytes),
            ("state_secondary", self.state_secondary_base, self.state_record_size_bytes),
        )

    def qspi_regions(self) -> tuple[tuple[str, int, int], ...]:
        return (
            ("qspi_metadata", self.qspi_metadata_offset, self.qspi_metadata_size_bytes),
            ("qspi_candidate", self.qspi_candidate_offset, self.qspi_candidate_size_bytes),
            ("qspi_recovery", self.qspi_recovery_offset, self.qspi_recovery_size_bytes),
            ("qspi_model_a", self.qspi_model_a_offset, self.qspi_model_a_size_bytes),
            ("qspi_model_b", self.qspi_model_b_offset, self.qspi_model_b_size_bytes),
        )


def parse_layout_text(text: str) -> MemoryLayout:
    """Parse only top-level ``key: integer`` lines; this is not a YAML parser."""
    values: dict[str, int] = {}
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.split("#", maxsplit=1)[0].strip()
        if not line:
            continue
        match = _KEY_VALUE_PATTERN.fullmatch(line)
        if match is None:
            raise LayoutError(f"line {line_number}: expected a top-level key: value entry")
        key = match.group("key")
        if key not in _REQUIRED_KEYS:
            raise LayoutError(f"line {line_number}: unknown layout key {key!r}")
        if key in values:
            raise LayoutError(f"line {line_number}: duplicate layout key {key!r}")
        try:
            value = int(match.group("value"), 0)
        except ValueError as error:
            raise LayoutError(f"line {line_number}: {key} must be an integer") from error
        if value < 0:
            raise LayoutError(f"line {line_number}: {key} must not be negative")
        values[key] = value

    missing = sorted(_REQUIRED_KEYS.difference(values))
    if missing:
        raise LayoutError(f"missing layout keys: {', '.join(missing)}")
    return validate_layout(MemoryLayout(**values))


def load_layout(path: Path) -> MemoryLayout:
    """Load and validate one restricted layout file."""
    return parse_layout_text(Path(path).read_text(encoding="utf-8"))


def validate_layout(layout: MemoryLayout) -> MemoryLayout:
    """Return a checked layout or raise before any package can use it."""
    if layout.flash_size_bytes == 0:
        raise LayoutError("flash_size_bytes must be positive")
    if layout.bootloader_base != layout.flash_base:
        raise LayoutError("bootloader_base must equal flash_base")
    if layout.package_max_image_bytes > layout.application_size_bytes:
        raise LayoutError("package_max_image_bytes exceeds application region")
    if layout.state_record_size_bytes < _H743_STATE_SECTOR_BYTES:
        raise LayoutError("state records must each use at least one 128 KiB H743 Flash sector")
    if layout.state_record_size_bytes % _H743_STATE_SECTOR_BYTES:
        raise LayoutError("state_record_size_bytes must align to a 128 KiB H743 Flash sector")
    if (
        layout.state_primary_base % _H743_STATE_SECTOR_BYTES
        or layout.state_secondary_base % _H743_STATE_SECTOR_BYTES
    ):
        raise LayoutError("state record bases must align to a 128 KiB H743 Flash sector")

    regions = tuple(layout.regions())
    for name, base, size in regions:
        if size == 0:
            raise LayoutError(f"{name} size must be positive")
        end = base + size
        if base < layout.flash_base or end > layout.flash_end:
            raise LayoutError(f"{name} lies outside internal Flash")

    for index, (left_name, left_base, left_size) in enumerate(regions):
        left_end = left_base + left_size
        for right_name, right_base, right_size in regions[index + 1:]:
            right_end = right_base + right_size
            if left_base < right_end and right_base < left_end:
                raise LayoutError(f"{left_name} overlaps {right_name}")

    if (
        layout.qspi_size_bytes != _W25Q64_SIZE_BYTES
        or layout.qspi_page_size_bytes != _W25Q64_PAGE_SIZE_BYTES
        or layout.qspi_erase_block_size_bytes != _W25Q64_ERASE_BLOCK_SIZE_BYTES
    ):
        raise LayoutError("QSPI geometry must match the locked U3 W25Q64 profile")

    qspi_regions = layout.qspi_regions()
    for name, offset, size in qspi_regions:
        if size == 0:
            raise LayoutError(f"{name} size must be positive")
        if offset % layout.qspi_erase_block_size_bytes or size % layout.qspi_erase_block_size_bytes:
            raise LayoutError(f"{name} must align to the QSPI erase block")
        if offset + size > layout.qspi_size_bytes:
            raise LayoutError(f"{name} lies outside QSPI Flash")

    metadata_subregions = (
        ("qspi_model_state", layout.qspi_model_state_offset,
         layout.qspi_model_state_size_bytes),
        ("qspi_ai_result", layout.qspi_ai_result_offset,
         layout.qspi_ai_result_size_bytes),
        ("qspi_hil_scratch", layout.qspi_hil_scratch_offset,
         layout.qspi_hil_scratch_size_bytes),
    )
    metadata_end = layout.qspi_metadata_offset + layout.qspi_metadata_size_bytes
    for name, offset, size in metadata_subregions:
        if size == 0:
            raise LayoutError(f"{name} size must be positive")
        if offset % layout.qspi_erase_block_size_bytes or size % layout.qspi_erase_block_size_bytes:
            raise LayoutError(f"{name} must align to the QSPI erase block")
        if offset < layout.qspi_metadata_offset or offset + size > metadata_end:
            raise LayoutError(f"{name} lies outside qspi_metadata")
    for index, (left_name, left_offset, left_size) in enumerate(metadata_subregions):
        left_end = left_offset + left_size
        for right_name, right_offset, right_size in metadata_subregions[index + 1:]:
            if left_offset < right_offset + right_size and right_offset < left_end:
                raise LayoutError(f"{left_name} overlaps {right_name}")

    secondary_state_end = (
        layout.qspi_model_state_secondary_offset
        + layout.qspi_model_state_secondary_size_bytes
    )
    model_a_end = layout.qspi_model_a_offset + layout.qspi_model_a_size_bytes
    if (
        layout.qspi_model_state_secondary_size_bytes == 0
        or layout.qspi_model_state_secondary_offset
        % layout.qspi_erase_block_size_bytes
        or layout.qspi_model_state_secondary_size_bytes
        % layout.qspi_erase_block_size_bytes
        or layout.qspi_model_state_secondary_offset < layout.qspi_model_a_offset
        or secondary_state_end > model_a_end
    ):
        raise LayoutError("qspi_model_state_secondary must be an aligned tail of qspi_model_a")

    for index, (left_name, left_offset, left_size) in enumerate(qspi_regions):
        left_end = left_offset + left_size
        for right_name, right_offset, right_size in qspi_regions[index + 1:]:
            right_end = right_offset + right_size
            if left_offset < right_end and right_offset < left_end:
                raise LayoutError(f"{left_name} overlaps {right_name}")
    if qspi_regions != _LOCKED_QSPI_REGIONS:
        raise LayoutError("QSPI slots must match the locked U3 W25Q64 layout")
    if layout.package_max_image_bytes != (
        layout.qspi_candidate_size_bytes - FIRMWARE_MANIFEST_HEADER_BYTES
    ):
        raise LayoutError(
            "package_max_image_bytes must equal the candidate slot minus the manifest header"
        )
    return layout


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a Phase 11 memory layout file.")
    parser.add_argument("layout", type=Path, help="restricted key: value memory layout")
    arguments = parser.parse_args()
    try:
        layout = load_layout(arguments.layout)
    except (LayoutError, OSError) as error:
        print(f"memory layout error: {error}", file=sys.stderr)
        return 2
    print(
        "memory layout: PASS "
        f"app=0x{layout.application_base:08X}+0x{layout.application_size_bytes:08X} "
        f"qspi=0x{layout.qspi_size_bytes:08X}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
