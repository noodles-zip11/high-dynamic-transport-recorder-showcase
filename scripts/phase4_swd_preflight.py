"""Run the read-only Phase 4 SWD preflight from any working directory."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.phase4_manual_common import (  # noqa: E402
    EXPECTED_STLINK_SERIAL,
    collect_swd_preflight,
)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--programmer",
        required=True,
        type=Path,
        help="absolute STM32_Programmer_CLI.exe path",
    )
    parser.add_argument(
        "--probe-serial",
        required=True,
        help=f"explicit ST-Link serial (expected {EXPECTED_STLINK_SERIAL})",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        type=Path,
        help="absolute external evidence directory",
    )
    return parser


def _require_absolute(path: Path, option: str) -> Path:
    resolved = Path(path)
    if not resolved.is_absolute():
        raise ValueError(f"{option} must be an absolute path")
    return resolved


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        programmer = _require_absolute(args.programmer, "--programmer")
        output_dir = _require_absolute(args.output_dir, "--output-dir")
        result = collect_swd_preflight(
            programmer=programmer,
            probe_serial=args.probe_serial,
            output_dir=output_dir,
        )
    except (OSError, ValueError) as error:
        parser.error(str(error))
        return 2

    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if result.get("pass") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
