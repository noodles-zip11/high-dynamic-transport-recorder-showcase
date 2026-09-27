from __future__ import annotations

from pathlib import Path

from host.transport_recorder.app import create_main_window


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_ROOT = REPOSITORY_ROOT / "host" / "transport_recorder" / "protocol"
UI_ROOT = REPOSITORY_ROOT / "host" / "transport_recorder" / "ui"


def test_protocol_does_not_import_qt_or_repository() -> None:
    protocol_sources = "\n".join(
        path.read_text(encoding="utf-8") for path in PROTOCOL_ROOT.glob("*.py")
    )

    assert "PySide6" not in protocol_sources
    assert "repository" not in protocol_sources


def test_ui_does_not_construct_terp_frames() -> None:
    ui_sources = "\n".join(path.read_text(encoding="utf-8") for path in UI_ROOT.glob("*.py"))

    assert "encode_frame(" not in ui_sources
    assert "Frame(" not in ui_sources


def test_app_can_construct_without_a_connected_device(qtbot, tmp_path: Path) -> None:
    window = create_main_window(tmp_path)
    qtbot.addWidget(window)

    window.show()

    assert window.statusBar().currentMessage() == "No device connected"
    assert not window.download_action.isEnabled()
