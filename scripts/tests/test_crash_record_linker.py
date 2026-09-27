from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_ROOT = PROJECT_ROOT / "firmware"
BSP_ROOT = FIRMWARE_ROOT / "bsp" / "openmv4_h743"


def test_crash_record_overlay_freezes_retained_sram4_boundary() -> None:
    overlay = BSP_ROOT / "linker_scripts" / "crash_record_overlay.lds"
    text = overlay.read_text(encoding="utf-8")

    assert ".crash_record" in text
    assert "NOLOAD" in text
    assert "0x3800FE00" in text
    assert "512" in text
    assert "__crash_record_slot_a" in text
    assert "__crash_record_slot_b" in text
    assert "__crash_record_end" in text
    assert "0x38010000" in text


def test_crash_record_target_backend_is_feature_only() -> None:
    target_header = BSP_ROOT / "crash_record_target.h"
    target_source = BSP_ROOT / "crash_record_target.c"

    assert target_header.is_file()
    assert target_source.is_file()
    source = target_source.read_text(encoding="utf-8")
    assert "TRANSPORT_RELIABILITY_EVIDENCE_ENABLED" in source
    assert "CRASH_RECORD_V1_RETENTION_BASE" in source
    assert "CRASH_RECORD_TARGET_PROBE_OFFSET 132U" in source
    assert "CRASH_RECORD_TARGET_CACHE_LINE_BYTES 32U" in source
    assert "SCB->DCCMVAC" in source
    probe_start = source.index("static int crash_record_target_probe")
    probe_end = source.index(
        "int crash_record_target_init",
        probe_start,
    )
    probe = source[probe_start:probe_end]
    normalized_probe = "".join(probe.split())
    assert (
        "CRASH_RECORD_V1_RETENTION_BASE+CRASH_RECORD_TARGET_PROBE_OFFSET"
        in normalized_probe
    )
    assert "CRASH_RECORD_V1_RESERVED_BYTES" not in probe


def test_target_ack_cleans_the_ack_cache_line_before_returning() -> None:
    source = (BSP_ROOT / "crash_record_target.c").read_text(encoding="utf-8")
    helper_start = source.index("static void crash_record_target_flush_ack")
    helper_end = source.index("\n}\n", helper_start) + 3
    helper = source[helper_start:helper_end]
    ack_start = source.index("crash_record_status_t crash_record_target_ack")
    ack_end = source.index("\n}\n", ack_start) + 3
    ack = source[ack_start:ack_end]

    assert "CRASH_RECORD_V1_ACK_OFFSET" in helper
    assert "SCB->DCCMVAC" in helper
    assert "crash_record_target_barrier(NULL)" in helper
    assert "status = crash_record_ack" in ack
    assert "status == CRASH_RECORD_STATUS_OK" in ack
    assert "crash_record_target_flush_ack" in ack


def test_fault_entries_are_feature_only_and_cover_all_four_entries() -> None:
    entry = BSP_ROOT / "crash_fault_entry.S"
    sconstruct = (FIRMWARE_ROOT / "SConstruct").read_text(encoding="utf-8")

    assert entry.is_file()
    entry_text = entry.read_text(encoding="utf-8")
    for fault in ("HardFault", "MemManage", "BusFault", "UsageFault"):
        assert f"crash_record_{fault}_Handler" in entry_text
    assert "EXC_RETURN" in entry_text
    assert "crash_record_fault_capture" in entry_text
    assert "TRANSPORT_RELIABILITY_EVIDENCE_ENABLED" in entry_text
    assert "__wrap_" not in entry_text
    assert "--wrap=" not in sconstruct


def test_startup_symbol_substitutions_are_scoped_to_one_cloned_object() -> None:
    sconstruct = (FIRMWARE_ROOT / "SConstruct").read_text(encoding="utf-8")

    assert "startup_env = env.Clone()" in sconstruct
    assert "startup_env.Append(CPPDEFINES=[" in sconstruct
    for fault in ("HardFault", "MemManage", "BusFault", "UsageFault"):
        assert (
            f'("{fault}_Handler", "crash_record_{fault}_Handler")'
            in sconstruct
        )
    assert "external_object(startup_source, startup_env)" in sconstruct


def test_fault_capture_has_direct_reset_and_frame_guard() -> None:
    source = (BSP_ROOT / "crash_record_target.c").read_text(encoding="utf-8")

    assert "crash_record_fault_capture" in source
    assert "SCB->CFSR" in source
    assert "SCB->HFSR" in source
    assert "FRAME_UNREADABLE" in source
    assert "SYSRESETREQ" in source
    assert "__crash_record" in source


def test_fault_capture_uses_fixed_build_identity_bytes() -> None:
    source = (BSP_ROOT / "crash_record_target.c").read_text(encoding="utf-8")

    assert '#include "build_identity.h"' in source
    assert "TRANSPORT_GIT_REVISION" in source
    assert "CRASH_RECORD_BUILD_ID_BYTES 32U" in source
    assert "sizeof(crash_record_build_id_source)" in source
    assert "strlen" not in source
    assert "memcpy" not in source


def test_fault_context_checker_requires_final_vector_and_call_graph_checks() -> None:
    checker = PROJECT_ROOT / "scripts" / "check_crash_fault_context.ps1"

    assert checker.is_file()
    text = checker.read_text(encoding="utf-8")
    assert "transport_recorder.off.elf" in text
    assert "--start-address" in text
    assert "crash_record_HardFault_Handler" in text
    assert "crash_record_UsageFault_Handler" in text
    assert "Convert-LittleEndianWord" in text
    assert "crash_fault_entry.o" in text
    assert "crash_record_target.o" in text
    assert "crash_record.o" in text
    assert "memcpy" in text
    assert "rt_hw_hard_fault_exception" in text


def test_fault_context_checker_freezes_non_crash_linker_boundaries() -> None:
    checker = PROJECT_ROOT / "scripts" / "check_crash_fault_context.ps1"
    text = checker.read_text(encoding="utf-8")

    assert "Assert-LinkerBoundaries" in text
    for symbol in (
        "g_pfnVectors",
        "_sstack",
        "_estack",
        "__heap_end",
        "__dma_buffer_start",
        "__dma_buffer_end",
        "__d2_sram1_start",
        "__d2_sram1_end",
    ):
        assert symbol in text
    assert "__heap_start" in text
    assert "0x4000" in text
    assert "0x08020000" in text


def test_off_baseline_switch_has_an_explicit_copy_contract() -> None:
    script = PROJECT_ROOT / "scripts" / "build_firmware.ps1"
    text = script.read_text(encoding="utf-8")

    assert "[switch]$SaveReliabilityOffBaseline" in text
    assert "SaveReliabilityOffBaseline -and $ReliabilityEvidence" in text
    assert "transport_recorder.off.elf" in text
    assert "transport_recorder.off.bin" in text
    assert "transport_recorder.off.map" in text
    assert "compile_commands.off.json" in text
    assert "Copy-Item" in text


def test_retention_backend_does_not_depend_on_backup_sram_regulator() -> None:
    source = (BSP_ROOT / "crash_record_target.c").read_text(encoding="utf-8")

    assert "D3_BKPSRAM_BASE" not in source
    assert "PWR_CR2_BRRDY" not in source
    assert "CRASH_RECORD_V1_RETENTION_BASE" in source


def test_crash_record_header_freezes_every_field_offset() -> None:
    header = (FIRMWARE_ROOT / "app" / "reliability" / "crash_record.h")
    text = header.read_text(encoding="utf-8")
    expected_offsets = {
        "magic": 0,
        "format_version": 4,
        "header_length": 6,
        "record_length": 8,
        "sequence": 12,
        "fault_kind": 16,
        "capture_flags": 20,
        "exc_return": 24,
        "sp": 28,
        "r0": 32,
        "r1": 36,
        "r2": 40,
        "r3": 44,
        "r12": 48,
        "lr": 52,
        "pc": 56,
        "xpsr": 60,
        "cfsr": 64,
        "hfsr": 68,
        "shcsr": 72,
        "mmfar": 76,
        "bfar": 80,
        "reset_flags": 84,
        "build_id": 88,
        "crc32": 120,
        "commit_marker": 124,
    }
    for field, offset in expected_offsets.items():
        assert (
            f"offsetof(crash_record_v1_t, {field}) == {offset}U" in text
        )


def test_native_crash_record_test_contains_a_complete_128_byte_golden() -> None:
    source = (
        FIRMWARE_ROOT / "tests" / "native" / "test_crash_record.c"
    ).read_text(encoding="utf-8")

    assert "CRASH_RECORD_V1_GOLDEN_RECORD[128]" in source
    assert "sizeof(CRASH_RECORD_V1_GOLDEN_RECORD)" in source
    assert "index < sizeof(CRASH_RECORD_V1_GOLDEN_RECORD)" in source


def test_runtime_recovery_is_enabled_only_and_non_blocking() -> None:
    runtime = (FIRMWARE_ROOT / "app" / "runtime" / "app_runtime.c")
    target_header = BSP_ROOT / "crash_record_target.h"
    runtime_text = runtime.read_text(encoding="utf-8")
    header_text = target_header.read_text(encoding="utf-8")

    assert '#include "crash_record_target.h"' in runtime_text
    assert "runtime_crash_record_startup" in runtime_text
    assert runtime_text.index("runtime_crash_record_startup();") < runtime_text.index(
        "power_runtime_start()"
    )
    assert "crash_record_target_init()" in runtime_text
    assert "crash_record_target_recover()" in runtime_text
    assert "CRASH_RECORD_TARGET_OK" in runtime_text
    for symbol in (
        "crash_record_target_get_visible_record",
        "crash_record_target_get_visible_slot",
        "crash_record_target_ack",
    ):
        assert symbol in header_text
