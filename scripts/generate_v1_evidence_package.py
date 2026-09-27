"""Generate or verify the public V1 evidence package without copying raw events."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Sequence


_REPO_ROOT = Path(__file__).resolve().parents[1]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from host.tools.v1_evidence_package import (  # noqa: E402
    DEFAULT_EVENT_IDS,
    generate_package,
    verify_package,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate or verify evidence/releases/v1.0.0 without copying QSPI raw files."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path("."),
        help="repository root containing main and tag v1.0.0 (default: current directory)",
    )
    parser.add_argument(
        "--source-backup",
        type=Path,
        help=(
            "read-only full-qspi-event-backup directory containing summary.json and event "
            "files; required when generating"
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("evidence/releases/v1.0.0"),
        help="package output directory (default: evidence/releases/v1.0.0)",
    )
    parser.add_argument(
        "--event-id",
        type=int,
        nargs="+",
        dest="event_ids",
        help=f"selected event IDs; defaults to {','.join(map(str, DEFAULT_EVENT_IDS))}",
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="verify an existing package instead of regenerating it",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="refresh generated files in an existing package directory",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    output = args.output
    if args.verify and args.force:
        raise SystemExit("--verify and --force cannot be combined")
    if args.verify:
        if not output.is_dir():
            raise SystemExit(f"cannot verify missing package directory: {output}")
        result = verify_package(output)
        action = "verified"
    else:
        if args.source_backup is None:
            raise SystemExit("--source-backup is required when generating a package")
        event_ids = tuple(args.event_ids) if args.event_ids else DEFAULT_EVENT_IDS
        result = generate_package(
            args.repo_root,
            args.source_backup,
            output,
            event_ids=event_ids,
            overwrite=args.force,
        )
        action = "generated-and-verified"
    print(json.dumps({"action": action, **result}, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
