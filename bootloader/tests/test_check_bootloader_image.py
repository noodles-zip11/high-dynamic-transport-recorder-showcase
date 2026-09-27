from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from check_bootloader_image import (
    BootloaderImageError,
    run_tool,
    validate_loadable_sections,
)


def test_loadable_sections_must_stay_inside_bootloader_rom() -> None:
    valid_sections = [
        (".isr_vector", 0x400, 0x08000000, 0x08000000, True, True),
        (".data", 0x20, 0x24000000, 0x08005000, True, True),
        (".bss", 0x400, 0x24000020, 0x24000020, True, False),
    ]

    assert validate_loadable_sections(valid_sections) == 0x5020

    with pytest.raises(BootloaderImageError, match="application Flash"):
        validate_loadable_sections(
            [(".text", 4, 0x08020000, 0x08020000, True, True)]
        )


def test_tool_output_is_decoded_as_utf8_on_windows(monkeypatch: pytest.MonkeyPatch) -> None:
    captured: dict[str, object] = {}

    class Completed:
        returncode = 0
        stdout = "objdump output"
        stderr = ""

    def fake_run(*args: object, **kwargs: object) -> Completed:
        captured["args"] = args
        captured["kwargs"] = kwargs
        return Completed()

    monkeypatch.setattr("check_bootloader_image.subprocess.run", fake_run)

    assert run_tool(["arm-none-eabi-objdump", "-h", "bootloader.elf"]) == "objdump output"
    assert captured["kwargs"] == {
        "check": False,
        "capture_output": True,
        "text": True,
        "encoding": "utf-8",
        "errors": "replace",
    }
