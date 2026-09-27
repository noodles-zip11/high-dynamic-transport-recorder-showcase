"""PySide6 desktop application entry point."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from PySide6.QtWidgets import QApplication

from .device.session import DeviceSession
from .protocol.serial_transport import SerialTransport
from .repository.event_repository import EventRepository
from .ui.main_window import MainWindow


def create_main_window(data_root: Path) -> MainWindow:
    return MainWindow(
        repository=EventRepository(data_root),
        session_factory=lambda port: DeviceSession(lambda: SerialTransport.open(port)),
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="transport-recorder-desktop")
    parser.add_argument("--data-root", type=Path, default=Path.home() / "TransportRecorder")
    args = parser.parse_args(argv)
    application = QApplication.instance() or QApplication(sys.argv)
    window = create_main_window(args.data_root)
    window.show()
    return application.exec()


if __name__ == "__main__":
    raise SystemExit(main())
