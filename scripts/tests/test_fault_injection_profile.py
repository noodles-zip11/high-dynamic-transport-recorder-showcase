from __future__ import annotations

from pathlib import Path
import re
import sys

import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_ROOT = PROJECT_ROOT / "firmware"
sys.path.insert(0, str(FIRMWARE_ROOT))


def test_build_profiles_are_exactly_debug_release_fault_injection() -> None:
    from build_options import resolve_build_profile

    assert resolve_build_profile("Debug") == "debug"
    assert resolve_build_profile("Release") == "release"
    assert resolve_build_profile("FaultInjection") == "faultinjection"
    with pytest.raises(RuntimeError, match="TRANSPORT_BUILD_PROFILE"):
        resolve_build_profile("test")


def test_fault_injection_forces_reliability_and_is_not_otherwise_selectable(
) -> None:
    from build_options import (
        resolve_fault_injection_enabled,
        resolve_reliability_evidence_enabled,
    )

    assert resolve_reliability_evidence_enabled({}, "faultinjection")
    assert resolve_fault_injection_enabled({}, "faultinjection")
    assert not resolve_fault_injection_enabled({}, "debug")
    assert not resolve_fault_injection_enabled({}, "release")
    with pytest.raises(RuntimeError, match="fault_injection"):
        resolve_fault_injection_enabled(
            {"fault_injection": "1"}, "debug"
        )


def test_fault_injection_source_is_profile_boundary_isolated() -> None:
    sconstruct = (FIRMWARE_ROOT / "SConstruct").read_text(encoding="utf-8")
    app_sconscript = (
        FIRMWARE_ROOT / "app" / "SConscript"
    ).read_text(encoding="utf-8")

    assert "FAULT_INJECTION_ENABLED" in sconstruct
    assert "TRANSPORT_FAULT_INJECTION_ENABLED" in sconstruct
    assert "fault_injection.c" in app_sconscript
    assert "TRANSPORT_FAULT_INJECTION_ENABLED" in app_sconscript
    assert "if env.get(\"TRANSPORT_FAULT_INJECTION_ENABLED\", False)" in (
        app_sconscript
    )


def test_fault_injection_command_contract_is_feature_only() -> None:
    source = (
        FIRMWARE_ROOT / "app" / "reliability" / "fault_injection.c"
    ).read_text(encoding="utf-8")
    header = (
        FIRMWARE_ROOT / "app" / "reliability" / "fault_injection.h"
    ).read_text(encoding="utf-8")

    assert "reliability_inject" in source
    assert "reliability_inject" in header
    assert "CONFIRM_RESET" in source
    assert "hardfault" in source
    assert "memmanage" in source
    assert "busfault" in source
    assert "usagefault" in source
    assert "msp-basic" in source
    assert "psp-fpu" in source
    assert "handler-basic" in source
    assert "rt_timer_init" in source
    assert "rt_timer_start" in source
    assert "RT_TIMER_FLAG_ONE_SHOT" in source
    assert "RT_TIMER_FLAG_HARD_TIMER" in source
    assert "RT_USING_TIMER_ALL_SOFT" in source
    assert "rt_pin_attach_irq" not in source
    assert "GET_PIN" not in source
    assert "__HAL_GPIO_EXTI_GENERATE_SWIT" not in source
    assert "GPIO_PIN_0" not in source
    assert "PIN_IRQ_MODE" not in source
    assert "rtdevice.h" not in source
    assert '"board.h"' not in source
    assert "crash_record_HardFault_Handler" not in source
    assert "crash_record_MemManage_Handler" not in source
    assert "crash_record_BusFault_Handler" not in source
    assert "crash_record_UsageFault_Handler" not in source


def test_fault_injection_runtime_registers_event_seam() -> None:
    runtime = (
        FIRMWARE_ROOT / "app" / "runtime" / "app_runtime.c"
    ).read_text(encoding="utf-8")
    source = (
        FIRMWARE_ROOT / "app" / "reliability" / "fault_injection.c"
    ).read_text(encoding="utf-8")
    quality = (
        FIRMWARE_ROOT / "app" / "event" / "event_quality.c"
    ).read_text(encoding="utf-8")
    event_service = (
        FIRMWARE_ROOT / "app" / "event" / "event_service.c"
    ).read_text(encoding="utf-8")

    assert "fault_injection_register_event_quality_hook" in runtime
    assert "fault_injection_register_event_quality_hook" in source
    assert "event_service_inject_quality_case" in source
    assert "event_service_inject_quality_case" in event_service
    for case in (
        "sequence-gap",
        "pretrigger-short",
        "duration-cap",
        "pool-pressure",
        "queue-pressure",
    ):
        assert case in quality
    assert "physical_sensor" not in source


def test_build_wrapper_exposes_fault_injection_profile() -> None:
    build_script = (
        PROJECT_ROOT / "scripts" / "build_firmware.ps1"
    ).read_text(encoding="utf-8-sig")

    assert "'FaultInjection'" in build_script
    assert "TRANSPORT_BUILD_PROFILE" in build_script
    assert "SaveReliabilityOffBaseline" in build_script


def test_reliability_event_stack_is_profile_isolated() -> None:
    source = (
        FIRMWARE_ROOT / "app" / "event" / "event_service.c"
    ).read_text(encoding="utf-8")

    assert re.search(
        r"#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED\s+"
        r"#define EVENT_SERVICE_THREAD_STACK_SIZE 4096U\s+"
        r"#else\s+"
        r"#define EVENT_SERVICE_THREAD_STACK_SIZE 2048U\s+"
        r"#endif",
        source,
    )


def test_legacy_ai_result_is_filtered_by_persisted_quality() -> None:
    service = (
        FIRMWARE_ROOT / "app" / "transport" / "terp_service.c"
    ).read_text(encoding="utf-8")

    assert "reliability_evidence_check_ai_result_access" in service
    assert "-TERP_ERROR_NOT_FOUND" in service
