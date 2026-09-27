from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable


BOOTLOADER_ROM_BASE = 0x08000000
BOOTLOADER_ROM_BYTES = 0x00020000
APPLICATION_BASE = 0x08020000
APPLICATION_END = 0x081C0000


class BootloaderImageError(RuntimeError):
    pass


Section = tuple[str, int, int, int, bool, bool]


def validate_loadable_sections(sections: Iterable[Section]) -> int:
    highest_rom_end = BOOTLOADER_ROM_BASE
    bootloader_rom_end = BOOTLOADER_ROM_BASE + BOOTLOADER_ROM_BYTES

    for name, size, _vma, lma, allocated, loaded in sections:
        if not allocated or not loaded or size == 0:
            continue
        section_end = lma + size
        if APPLICATION_BASE <= lma < APPLICATION_END:
            raise BootloaderImageError(
                f"{name} loads in application Flash at 0x{lma:08X}"
            )
        if lma < BOOTLOADER_ROM_BASE or section_end > bootloader_rom_end:
            raise BootloaderImageError(
                f"{name} load range 0x{lma:08X}..0x{section_end - 1:08X} "
                "is outside Bootloader ROM"
            )
        highest_rom_end = max(highest_rom_end, section_end)
    return highest_rom_end - BOOTLOADER_ROM_BASE


def parse_objdump_sections(output: str) -> list[Section]:
    sections: list[Section] = []
    header = re.compile(
        r"^\s*\d+\s+(\S+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+"
    )
    lines = output.splitlines()
    for index, line in enumerate(lines):
        match = header.match(line)
        if match is None:
            continue
        flags = lines[index + 1] if index + 1 < len(lines) else ""
        sections.append(
            (
                match.group(1),
                int(match.group(2), 16),
                int(match.group(3), 16),
                int(match.group(4), 16),
                "ALLOC" in flags,
                "LOAD" in flags,
            )
        )
    if not sections:
        raise BootloaderImageError("objdump returned no ELF sections")
    return sections


def validate_map_layout(map_path: Path) -> None:
    text = map_path.read_text(encoding="utf-8", errors="replace")
    match = re.search(
        r"^ROM\s+0x0*8000000\s+0x0*20000\b", text, flags=re.MULTILINE
    )
    if match is None:
        raise BootloaderImageError(
            "link map does not reserve ROM at 0x08000000 with length 128 KiB"
        )


def validate_symbols(output: str) -> None:
    forbidden: list[str] = []
    for line in output.splitlines():
        fields = line.split()
        if not fields:
            continue
        symbol = fields[-1].lower()
        if symbol.startswith(("rt_", "_rt_", "rtt_", "rtthread")):
            forbidden.append(fields[-1])
    if forbidden:
        raise BootloaderImageError(
            "RT-Thread symbols found in Bootloader: " + ", ".join(sorted(set(forbidden)))
        )


def run_tool(command: list[str]) -> str:
    completed = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        raise BootloaderImageError(
            f"tool failed ({' '.join(command)}): {completed.stderr.strip()}"
        )
    return completed.stdout


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify that the standalone Bootloader stays inside its reservation."
    )
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--map", dest="map_path", required=True, type=Path)
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    args = parser.parse_args(argv)

    try:
        if not args.elf.is_file() or not args.map_path.is_file():
            raise BootloaderImageError("Bootloader ELF or map file is missing")
        validate_map_layout(args.map_path)
        sections = parse_objdump_sections(run_tool([args.objdump, "-h", str(args.elf)]))
        image_bytes = validate_loadable_sections(sections)
        validate_symbols(run_tool([args.nm, "--defined-only", str(args.elf)]))
    except (BootloaderImageError, OSError) as error:
        print(f"Bootloader image check: FAIL: {error}", file=sys.stderr)
        return 2

    print(
        "Bootloader image check: PASS "
        f"({image_bytes} bytes used of {BOOTLOADER_ROM_BYTES} bytes; no RT-Thread symbols)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
