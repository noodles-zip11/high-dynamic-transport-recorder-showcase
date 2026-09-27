#!/usr/bin/env python3
"""Run the resumable ten-round RC-013 physical cold-boot matrix."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import subprocess
import sys
import time
from typing import Callable, TypeAlias


REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts.phase4_manual_common import (  # noqa: E402
    _rc013_readback_failure,
    _write_json_once,
    collect_rc013_readback,
)


ReadbackFn: TypeAlias = Callable[[str, Path, str], dict[str, object]]


def _timestamp() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _source_revision() -> str | None:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        return None
    return completed.stdout.strip() or None


def _next_label_number(output_dir: Path, prefix: str) -> int:
    number = 1
    while (output_dir / f"{prefix}-{number:02d}.json").exists():
        number += 1
    return number


def _load_existing_round(path: Path, expected_round: int) -> str | None:
    try:
        row = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return "existing round record is unreadable or invalid JSON"
    if not isinstance(row, dict):
        return "existing round record is not a JSON object"
    if row.get("schema") != "phase4-rc013-cold-boot-round-v1":
        return "existing round record has the wrong schema"
    if row.get("round") != expected_round:
        return "existing round record has the wrong identity"
    if not (
        row.get("port") == "COM13"
        and row.get("status") == "PASS"
        and row.get("physical_confirmation") is True
        and row.get("formal_pass_eligible") is True
    ):
        return "existing round is not an eligible immutable PASS"
    if row.get("off_confirmation") != "OFF":
        return "existing round lacks the exact OFF confirmation"
    if row.get("ready_confirmation") != "READY":
        return "existing round lacks the exact READY confirmation"
    for timestamp_field in (
        "started_at_utc",
        "off_confirmed_at_utc",
        "ready_confirmed_at_utc",
        "ended_at_utc",
    ):
        timestamp = row.get(timestamp_field)
        if not isinstance(timestamp, str) or not timestamp.strip():
            return f"existing round lacks nonempty {timestamp_field}"
    off_seconds = row.get("off_seconds")
    if (
        isinstance(off_seconds, bool)
        or not isinstance(off_seconds, (int, float))
        or not math.isfinite(off_seconds)
        or off_seconds < 5.0
    ):
        return "existing round power-off interval is below five seconds"
    readback = row.get("readback")
    if not isinstance(readback, dict):
        return "existing round readback is missing"
    readback_error = _rc013_readback_failure(readback)
    if readback_error is not None:
        return f"existing round readback is invalid: {readback_error}"
    return None


def _existing_aggregate_failure(
    path: Path,
    completed_rounds: list[int],
) -> tuple[dict[str, object] | None, str | None]:
    try:
        aggregate = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None, "existing aggregate is unreadable or invalid JSON"
    expected_rounds = list(range(1, 11))
    if not isinstance(aggregate, dict):
        return None, "existing aggregate is not a JSON object"
    if aggregate.get("schema") != "phase4-rc013-cold-boot-matrix-v1":
        return aggregate, "existing aggregate has the wrong schema"
    if aggregate.get("pass") is not True:
        return aggregate, "existing aggregate does not report pass=true"
    if aggregate.get("status") != "PASS":
        return aggregate, "existing aggregate status is not PASS"
    if aggregate.get("port") != "COM13":
        return aggregate, "existing aggregate port is not COM13"
    if aggregate.get("physical_confirmation") is not True:
        return aggregate, "existing aggregate lacks physical confirmation"
    if aggregate.get("formal_pass_eligible") is not True:
        return aggregate, "existing aggregate is not formal-pass eligible"
    if aggregate.get("rounds") != 10:
        return aggregate, "existing aggregate does not declare ten rounds"
    if aggregate.get("completed_rounds") != expected_rounds:
        return aggregate, "existing aggregate does not contain rounds 1 through 10"
    if completed_rounds != expected_rounds:
        return aggregate, "existing aggregate lacks ten validated round records"
    preflight = aggregate.get("preflight")
    if not isinstance(preflight, dict):
        return aggregate, "existing aggregate preflight is missing"
    preflight_error = _rc013_readback_failure(preflight)
    if preflight_error is not None:
        return aggregate, f"existing aggregate preflight is invalid: {preflight_error}"
    return aggregate, None


def _write_attempt_result(
    output_dir: Path,
    attempt_number: int,
    result: dict[str, object],
) -> None:
    _write_json_once(
        output_dir / f"attempt-{attempt_number:02d}.json", result
    )


def run_cold_boot_matrix(
    port: str,
    output_dir: Path,
    rounds: int = 10,
    diagnostic: bool = False,
    input_fn: Callable[[str], str] = input,
    clock: Callable[[], float] = time.monotonic,
    sleep_fn: Callable[[float], None] = time.sleep,
    readback_fn: ReadbackFn = collect_rc013_readback,
) -> dict[str, object]:
    """Run or resume a fail-fast RC-013 cold-boot matrix."""

    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    started_at = _timestamp()
    base_result: dict[str, object] = {
        "schema": "phase4-rc013-cold-boot-matrix-v1",
        "pass": False,
        "port": port,
        "rounds": rounds,
        "source_revision": _source_revision(),
        "started_at_utc": started_at,
        "physical_confirmation": False,
        "formal_pass_eligible": False,
    }
    if port.upper() != "COM13":
        return {
            **base_result,
            "status": "INVALID_ARGUMENT",
            "error": "port must be the frozen explicit COM13",
            "ended_at_utc": _timestamp(),
        }
    if rounds != 10:
        return {
            **base_result,
            "status": "INVALID_ARGUMENT",
            "error": "formal cold-boot matrix requires exactly 10 rounds",
            "ended_at_utc": _timestamp(),
        }

    if diagnostic:
        diagnostic_number = _next_label_number(output_dir, "diagnostic")
        label = f"diagnostic-{diagnostic_number:02d}"
        try:
            readback = readback_fn(port, output_dir, label)
            error = _rc013_readback_failure(readback)
        except Exception as exception:
            readback = None
            error = f"{type(exception).__name__}: {exception}"
        result = {
            **base_result,
            "status": "DIAGNOSTIC_NOT_PHYSICAL",
            "diagnostic_readback_pass": error is None,
            "readback": readback,
            "ended_at_utc": _timestamp(),
        }
        if error is not None:
            result["error"] = error
        _write_json_once(
            output_dir / f"diagnostic-result-{diagnostic_number:02d}.json",
            result,
        )
        return result

    completed_rounds: list[int] = []
    for round_number in range(1, rounds + 1):
        round_path = output_dir / f"round-{round_number:02d}.json"
        if not round_path.exists():
            continue
        blocker = _load_existing_round(round_path, round_number)
        if blocker is not None:
            return {
                **base_result,
                "status": "BLOCKED_EXISTING_ROUND",
                "error": f"round-{round_number:02d}: {blocker}",
                "blocked_path": str(round_path),
                "ended_at_utc": _timestamp(),
            }
        completed_rounds.append(round_number)

    results_path = output_dir / "results.json"
    if results_path.exists():
        aggregate, aggregate_error = _existing_aggregate_failure(
            results_path, completed_rounds
        )
        if aggregate_error is not None:
            return {
                **base_result,
                "status": "BLOCKED_EXISTING_AGGREGATE",
                "error": aggregate_error,
                "blocked_path": str(results_path),
                "ended_at_utc": _timestamp(),
            }
        assert aggregate is not None
        return aggregate

    attempt_number = _next_label_number(output_dir, "attempt")
    preflight_label = f"preflight-{attempt_number:02d}"
    try:
        preflight = readback_fn(port, output_dir, preflight_label)
        preflight_error = _rc013_readback_failure(preflight)
    except Exception as exception:
        preflight = None
        preflight_error = f"{type(exception).__name__}: {exception}"
    if preflight_error is not None:
        result = {
            **base_result,
            "status": "PREFLIGHT_FAIL",
            "error": preflight_error,
            "preflight": preflight,
            "completed_rounds": completed_rounds,
            "ended_at_utc": _timestamp(),
        }
        _write_attempt_result(output_dir, attempt_number, result)
        return result

    for round_number in range(1, rounds + 1):
        if round_number in completed_rounds:
            continue
        round_path = output_dir / f"round-{round_number:02d}.json"
        row: dict[str, object] = {
            "schema": "phase4-rc013-cold-boot-round-v1",
            "round": round_number,
            "status": "FAIL",
            "port": port,
            "physical_confirmation": False,
            "formal_pass_eligible": False,
            "started_at_utc": _timestamp(),
        }

        off_confirmation = input_fn(
            f"{round_number}/{rounds}: remove board main power; when the "
            "power indicator is OFF, enter OFF: "
        )
        row["off_confirmation"] = off_confirmation
        if off_confirmation != "OFF":
            row["error"] = "exact OFF confirmation was not received"
            row["ended_at_utc"] = _timestamp()
            _write_json_once(round_path, row)
            result = {
                **base_result,
                "status": "FAIL",
                "error": f"round-{round_number:02d}: {row['error']}",
                "preflight": preflight,
                "failed_round": round_number,
                "ended_at_utc": _timestamp(),
            }
            _write_attempt_result(output_dir, attempt_number, result)
            return result

        off_started = clock()
        row["off_confirmed_at_utc"] = _timestamp()
        sleep_fn(5.0)
        ready_confirmation = input_fn(
            f"{round_number}/{rounds}: restore board main power; after power "
            "is stable, enter READY: "
        )
        off_ended = clock()
        row["ready_confirmation"] = ready_confirmation
        row["ready_confirmed_at_utc"] = _timestamp()
        row["off_seconds"] = max(0.0, off_ended - off_started)
        if ready_confirmation != "READY":
            row["error"] = "exact READY confirmation was not received"
        elif row["off_seconds"] < 5.0:
            row["error"] = "recorded power-off interval is below five seconds"
        else:
            row["physical_confirmation"] = True
            row["formal_pass_eligible"] = True
            label = f"round-{round_number:02d}-readback"
            try:
                readback = readback_fn(port, output_dir, label)
                readback_error = _rc013_readback_failure(readback)
            except Exception as exception:
                readback = None
                readback_error = f"{type(exception).__name__}: {exception}"
            row["readback"] = readback
            if readback_error is None:
                row["status"] = "PASS"
            else:
                row["error"] = readback_error

        row["ended_at_utc"] = _timestamp()
        _write_json_once(round_path, row)
        if row["status"] != "PASS":
            result = {
                **base_result,
                "status": "FAIL",
                "error": f"round-{round_number:02d}: {row['error']}",
                "preflight": preflight,
                "failed_round": round_number,
                "ended_at_utc": _timestamp(),
            }
            _write_attempt_result(output_dir, attempt_number, result)
            return result
        completed_rounds.append(round_number)

    result = {
        **base_result,
        "pass": True,
        "status": "PASS",
        "physical_confirmation": True,
        "formal_pass_eligible": True,
        "preflight": preflight,
        "completed_rounds": sorted(completed_rounds),
        "ended_at_utc": _timestamp(),
    }
    _write_attempt_result(output_dir, attempt_number, result)
    _write_json_once(results_path, result)
    return result


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="must be explicit COM13")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--rounds", type=int, choices=(10,), default=10)
    parser.add_argument(
        "--diagnostic",
        action="store_true",
        help="read-only path/device check; never a formal physical PASS",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.output_dir.is_absolute():
        print("--output-dir must be an absolute path", file=sys.stderr)
        return 2
    result = run_cold_boot_matrix(
        port=args.port,
        output_dir=args.output_dir,
        rounds=args.rounds,
        diagnostic=args.diagnostic,
    )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("pass") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
