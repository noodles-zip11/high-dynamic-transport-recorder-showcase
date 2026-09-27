"""Pure build-option policy shared by SCons and host-side tests."""

from __future__ import annotations

from collections.abc import Mapping


BUILD_PROFILES = {"debug", "release", "faultinjection"}


def resolve_build_profile(value: str) -> str:
    profile = value.lower()
    if profile not in BUILD_PROFILES:
        raise RuntimeError(
            "TRANSPORT_BUILD_PROFILE must be 'debug', 'release', or "
            f"'faultinjection', got {value!r}"
        )
    return profile


def _resolve_binary_option(
    arguments: Mapping[str, str], name: str, default: str
) -> bool:
    value = arguments.get(name, default)
    if value not in {"0", "1"}:
        raise RuntimeError(f"{name} must be '0' or '1'")
    return value == "1"


def resolve_reliability_evidence_enabled(
    arguments: Mapping[str, str],
    build_profile: str,
    *,
    release_manifest_valid: bool = False,
) -> bool:
    profile = resolve_build_profile(build_profile)
    enabled = _resolve_binary_option(arguments, "reliability_evidence", "0")
    if profile == "faultinjection":
        return True
    if enabled and profile == "release" and not release_manifest_valid:
        raise RuntimeError(
            "Release reliability evidence remains locked until Phase 4 "
            "manifest validation"
        )
    return enabled


def resolve_fault_injection_enabled(
    arguments: Mapping[str, str], build_profile: str
) -> bool:
    profile = resolve_build_profile(build_profile)
    requested = _resolve_binary_option(arguments, "fault_injection", "0")
    if requested and profile != "faultinjection":
        raise RuntimeError(
            "fault_injection is only selectable by the FaultInjection profile"
        )
    return profile == "faultinjection"
