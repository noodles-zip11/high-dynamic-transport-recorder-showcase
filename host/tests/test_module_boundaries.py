from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


def test_acquisition_does_not_depend_on_event_module() -> None:
    acquisition_source = (
        REPOSITORY_ROOT / "firmware" / "app" / "acquisition" / "imu_acquisition.c"
    ).read_text(encoding="utf-8")
    acquisition_header = (
        REPOSITORY_ROOT / "firmware" / "app" / "acquisition" / "imu_acquisition.h"
    ).read_text(encoding="utf-8")

    assert '"event_service.h"' not in acquisition_source
    assert '"event_export_debug.h"' not in acquisition_header
    assert "event_assembler_t" not in acquisition_header


def test_event_does_not_depend_on_storage_implementation() -> None:
    event_source = (
        REPOSITORY_ROOT / "firmware" / "app" / "event" / "event_service.c"
    ).read_text(encoding="utf-8")

    assert '"storage_service.h"' not in event_source
    assert "storage_service_" not in event_source
