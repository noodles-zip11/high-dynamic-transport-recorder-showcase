from __future__ import annotations

import ast
import os
from pathlib import Path
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_ROOT = PROJECT_ROOT / "firmware"


def _rtconfig_values(profile: str) -> tuple[str, str, str]:
    environment = os.environ.copy()
    environment["TRANSPORT_BUILD_PROFILE"] = profile
    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            "import rtconfig; print(repr(rtconfig.CFLAGS)); "
            "print(repr(rtconfig.CXXFLAGS)); print(repr(rtconfig.AFLAGS))",
        ],
        cwd=FIRMWARE_ROOT,
        env=environment,
        check=True,
        capture_output=True,
        text=True,
    )
    return tuple(ast.literal_eval(line) for line in completed.stdout.splitlines())


def test_release_profile_removes_debug_flags_and_defines_release_mode() -> None:
    cflags, cxxflags, aflags = _rtconfig_values("release")

    assert "-O2" in cflags
    assert "-O0" not in cflags
    assert "-gdwarf" not in cflags
    assert "-g0" in cflags
    assert "-O2" in cxxflags
    assert "-O0" not in cxxflags
    assert "-gdwarf" not in cxxflags
    assert "-g0" in cxxflags
    assert "-gdwarf" not in aflags
    assert "-DTRANSPORT_BUILD_RELEASE=1" in cflags
