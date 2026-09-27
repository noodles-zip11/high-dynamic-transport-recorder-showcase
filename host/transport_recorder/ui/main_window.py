"""Main-window composition; all protocol work is delegated to worker threads."""

from __future__ import annotations

from pathlib import Path
import time
from typing import Callable

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from ..analysis.event_record import DecodedEvent, load_event
from ..analysis.export import export_csv, export_json_summary, export_raw
from ..device.session import ConnectedDevice, DeviceSession, EventPage, SessionState
from ..protocol.client import AiResult, DownloadProgress
from ..protocol.serial_transport import list_serial_ports
from ..repository.event_repository import EventRepository, StoredEvent
from .replay_widget import ReplayWidget
from .workers import DownloadWorker, OperationError, OperationWorker


class MainWindow(QMainWindow):
    """Manage local replay plus a TERP device session without blocking Qt's thread."""

    event_loaded = Signal()

    def __init__(
        self,
        *,
        repository: EventRepository,
        session_factory: Callable[[str], DeviceSession | None],
    ) -> None:
        super().__init__()
        self._repository = repository
        self._session_factory = session_factory
        self._session: DeviceSession | None = None
        self._current_event: DecodedEvent | None = None
        self._current_raw_path: Path | None = None
        self._current_stored_event: StoredEvent | None = None
        self._next_event_id = 0
        self._workers: list[OperationWorker | DownloadWorker] = []
        self._device_workers: list[OperationWorker | DownloadWorker] = []
        self._download_started_at: float | None = None

        self.setWindowTitle("Transport Recorder")
        self.resize(1_200, 760)
        self.port_list = QTreeWidget()
        self.port_list.setHeaderLabels(["Port", "Description", "Hardware ID"])
        self.event_list = QTreeWidget()
        self.event_list.setHeaderLabels(["Event ID", "Bytes", "CRC32", "Status"])
        self.replay_widget = ReplayWidget()
        self._connection_summary = QLabel("No device connected")
        self._build_actions()
        self._build_layout()
        self.statusBar().showMessage("No device connected")

    def set_ports(self, ports: list[dict[str, str | None]]) -> None:
        self.port_list.clear()
        for port in ports:
            item = QTreeWidgetItem(
                [
                    port.get("device") or "",
                    port.get("description") or "",
                    port.get("serial_number") or "",
                ]
            )
            self.port_list.addTopLevelItem(item)

    def refresh_ports(self) -> None:
        self.set_ports(list_serial_ports())
        self.statusBar().showMessage(f"Found {self.port_list.topLevelItemCount()} serial ports")

    def open_local_event(self, path: Path) -> None:
        self._start_operation(lambda: (Path(path), load_event(Path(path))), self._on_local_event_loaded)

    def show_decoded_event(self, event: DecodedEvent, raw_path: Path | None = None) -> None:
        self._current_event = event
        self._current_raw_path = raw_path
        self._current_stored_event = (
            self._repository.find_event_by_path(raw_path) if raw_path is not None else None
        )
        self._show_annotation(self._current_stored_event)
        self._show_ai_result(
            self._current_stored_event.ai_result if self._current_stored_event else None
        )
        self._set_device_actions_busy(bool(self._device_workers))
        self.replay_widget.set_event(event)
        self.export_csv_action.setEnabled(True)
        self.export_json_action.setEnabled(True)
        self.export_png_action.setEnabled(True)
        self.export_raw_action.setEnabled(raw_path is not None)
        self.statusBar().showMessage(f"Opened event {event.metadata.event_id}")
        self.event_loaded.emit()

    def show_disconnected(self, reason: str) -> None:
        self.download_action.setEnabled(False)
        self.download_button.setEnabled(False)
        self.load_more_button.setEnabled(False)
        self.cancel_download_action.setEnabled(False)
        self.read_ai_result_button.setEnabled(False)
        self._connection_summary.setText("No device connected")
        self.statusBar().showMessage(f"Disconnected: {reason}")

    def closeEvent(self, event) -> None:  # type: ignore[override]
        for worker in tuple(self._workers):
            if isinstance(worker, DownloadWorker):
                worker.cancel()
            worker.wait(2_000)
        if self._session is not None:
            self._session.close()
        event.accept()

    def _build_actions(self) -> None:
        self.open_action = QAction("Open local event", self)
        self.open_action.triggered.connect(self._choose_local_event)
        self.refresh_action = QAction("Refresh ports", self)
        self.refresh_action.triggered.connect(self.refresh_ports)
        self.connect_action = QAction("Connect selected port", self)
        self.connect_action.triggered.connect(self._connect_selected_port)
        self.download_action = QAction("Download selected event", self)
        self.download_action.setEnabled(False)
        self.download_action.triggered.connect(self._download_selected_event)
        self.cancel_download_action = QAction("Cancel download", self)
        self.cancel_download_action.setEnabled(False)
        self.cancel_download_action.triggered.connect(self._cancel_download)
        self.delete_unavailable_action = QAction("Delete device event (unsupported)", self)
        self.delete_unavailable_action.setEnabled(False)
        self.delete_unavailable_action.setToolTip("EL01 deletion and garbage collection are not designed")
        self.export_csv_action = QAction("Export CSV", self)
        self.export_json_action = QAction("Export JSON summary", self)
        self.export_png_action = QAction("Export PNG", self)
        self.export_raw_action = QAction("Export raw file", self)
        for action in (
            self.export_csv_action,
            self.export_json_action,
            self.export_png_action,
            self.export_raw_action,
        ):
            action.setEnabled(False)
        self.export_csv_action.triggered.connect(self._export_csv)
        self.export_json_action.triggered.connect(self._export_json)
        self.export_png_action.triggered.connect(self._export_png)
        self.export_raw_action.triggered.connect(self._export_raw)

    def _build_layout(self) -> None:
        file_menu = self.menuBar().addMenu("File")
        file_menu.addAction(self.open_action)
        export_menu = file_menu.addMenu("Export")
        export_menu.addActions(
            [
                self.export_csv_action,
                self.export_json_action,
                self.export_png_action,
                self.export_raw_action,
            ]
        )
        device_menu = self.menuBar().addMenu("Device")
        device_menu.addActions(
            [
                self.refresh_action,
                self.connect_action,
                self.download_action,
                self.cancel_download_action,
                self.delete_unavailable_action,
            ]
        )

        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.addWidget(QLabel("Candidate ports"))
        left_layout.addWidget(self.port_list)
        ports_buttons = QHBoxLayout()
        refresh_button = QPushButton("Refresh")
        refresh_button.clicked.connect(self.refresh_ports)
        self.connect_button = QPushButton("Connect")
        self.connect_button.clicked.connect(self._connect_selected_port)
        ports_buttons.addWidget(refresh_button)
        ports_buttons.addWidget(self.connect_button)
        left_layout.addLayout(ports_buttons)
        left_layout.addWidget(QLabel("Device events"))
        left_layout.addWidget(self.event_list)
        self.download_button = QPushButton("Download selected")
        self.download_button.setEnabled(False)
        self.download_button.clicked.connect(self._download_selected_event)
        left_layout.addWidget(self.download_button)
        self.load_more_button = QPushButton("Load more events")
        self.load_more_button.setEnabled(False)
        self.load_more_button.clicked.connect(self._load_more_events)
        left_layout.addWidget(self.load_more_button)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.addWidget(self._connection_summary)
        right_layout.addWidget(QLabel("Model result (device)"))
        self.ai_result_label = QLabel("No model result stored for this event")
        self.ai_result_label.setWordWrap(True)
        right_layout.addWidget(self.ai_result_label)
        self.read_ai_result_button = QPushButton("Read model result")
        self.read_ai_result_button.setEnabled(False)
        self.read_ai_result_button.clicked.connect(self._read_ai_result)
        right_layout.addWidget(self.read_ai_result_button)
        right_layout.addWidget(QLabel("Label"))
        self.annotation_label_edit = QLineEdit()
        right_layout.addWidget(self.annotation_label_edit)
        right_layout.addWidget(QLabel("Note"))
        self.annotation_note_edit = QPlainTextEdit()
        self.annotation_note_edit.setMaximumHeight(80)
        right_layout.addWidget(self.annotation_note_edit)
        self.save_annotation_button = QPushButton("Save annotation")
        self.save_annotation_button.clicked.connect(self._save_annotation_from_controls)
        right_layout.addWidget(self.save_annotation_button)
        self._show_annotation(None)
        right_layout.addWidget(self.replay_widget, stretch=1)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(left)
        splitter.addWidget(right)
        splitter.setStretchFactor(1, 1)
        self.setCentralWidget(splitter)

    def _choose_local_event(self) -> None:
        filename, _ = QFileDialog.getOpenFileName(
            self, "Open recorder event", str(Path.home()), "Recorder events (*.terp-event);;All files (*)"
        )
        if filename:
            self.open_local_event(Path(filename))

    def _connect_selected_port(self) -> None:
        item = self.port_list.currentItem()
        if item is None:
            self.statusBar().showMessage("Select a serial port before connecting")
            return
        session = self._session_factory(item.text(0))
        if session is None:
            self.show_disconnected("session factory did not create a device session")
            return
        self._session = session
        self._start_device_operation(session.connect, self._on_connected)

    def _on_connected(self, connected: object) -> None:
        assert isinstance(connected, ConnectedDevice)
        self._connection_summary.setText(
            " | ".join(
                [
                    f"Serial: {connected.identity.serial_number}",
                    f"Firmware: {connected.identity.firmware_version}",
                    f"Hardware: {connected.identity.hardware_version}",
                    f"Storage ready: {connected.health.storage_ready}",
                ]
            )
        )
        self.download_action.setEnabled(True)
        self._set_device_actions_busy(bool(self._device_workers))
        self.statusBar().showMessage(f"Connected to {connected.identity.serial_number}")
        if self._session is not None:
            self._request_event_page(after_event_id=0)

    def _request_event_page(self, *, after_event_id: int) -> None:
        if self._session is None:
            return
        self._start_device_operation(
            lambda: self._session.list_events(after_event_id=after_event_id, limit=16),
            lambda page: self._on_event_page(page, append=after_event_id != 0),
        )

    def _load_more_events(self) -> None:
        if self._next_event_id:
            self._request_event_page(after_event_id=self._next_event_id)

    def _on_event_page(self, page: object, *, append: bool = False) -> None:
        assert isinstance(page, EventPage)
        if not append:
            self.event_list.clear()
        for event in page.events:
            item = QTreeWidgetItem(
                [str(event.event_id), str(event.total_length), f"{event.event_crc32:08X}", "not downloaded"]
            )
            item.setData(0, Qt.ItemDataRole.UserRole, event.event_id)
            self.event_list.addTopLevelItem(item)
        self._next_event_id = page.next_event_id
        self._set_device_actions_busy(bool(self._device_workers))

    def _download_selected_event(self) -> None:
        item = self.event_list.currentItem()
        if item is None or self._session is None:
            self.statusBar().showMessage("Select an event after connecting")
            return
        event_id = int(item.data(0, Qt.ItemDataRole.UserRole))
        worker = DownloadWorker(
            lambda cancelled, progress: self._session.download_to_repository(
                event_id, self._repository, cancelled=cancelled, progress=progress
            )
        )
        worker.progress.connect(self._on_download_progress)
        worker.succeeded.connect(self._on_download_finished)
        worker.failed.connect(self._on_worker_failed)
        worker.cancelled.connect(lambda: self.statusBar().showMessage("Download cancelled; resumable data retained"))
        worker.finished.connect(self._finish_download)
        self.cancel_download_action.setEnabled(True)
        self._download_started_at = time.monotonic()
        self._start_device_worker(worker)

    def _cancel_download(self) -> None:
        for worker in self._workers:
            if isinstance(worker, DownloadWorker) and worker.isRunning():
                worker.cancel()
                self.statusBar().showMessage("Cancelling after the current verified chunk")
                return

    def _on_download_progress(self, progress: object) -> None:
        assert isinstance(progress, DownloadProgress)
        started_at = self._download_started_at or time.monotonic()
        elapsed_seconds = max(time.monotonic() - started_at, 0.001)
        bytes_per_second = progress.verified_bytes / elapsed_seconds
        remaining_seconds = (progress.total_bytes - progress.verified_bytes) / bytes_per_second
        self.statusBar().showMessage(
            f"Downloading event {progress.event_id}: "
            f"{progress.verified_bytes}/{progress.total_bytes} bytes "
            f"({bytes_per_second:.0f} B/s, ETA {remaining_seconds:.1f} s)"
        )

    def _on_download_finished(self, stored: object) -> None:
        assert isinstance(stored, StoredEvent)
        self.statusBar().showMessage(f"Event {stored.key.event_id} validated and indexed")
        self.open_local_event(stored.path)

    def _finish_download(self) -> None:
        self.cancel_download_action.setEnabled(False)
        self._download_started_at = None

    def _export_csv(self) -> None:
        self._choose_export_target("CSV files (*.csv)", lambda path: export_csv(self._required_event(), path))

    def _export_json(self) -> None:
        self._choose_export_target(
            "JSON files (*.json)",
            self.export_current_json,
        )

    def export_current_json(self, path: Path) -> Path:
        stored = self._current_stored_event
        if stored is not None:
            stored = self._repository.get_event(stored.key)
            self._current_stored_event = stored
        return export_json_summary(
            self._required_event(),
            path,
            validation_state=(stored.validation_state.value if stored else "valid"),
            label=stored.label if stored else None,
            note=stored.note if stored else None,
            ai_result=stored.ai_result if stored else None,
        )

    def save_current_annotation(self, *, label: str | None, note: str | None) -> None:
        stored = self._current_stored_event
        if stored is None:
            raise ValueError("the open event is not indexed in the local repository")
        self._repository.update_annotation(stored.key, label=label, note=note)
        self._current_stored_event = self._repository.get_event(stored.key)
        self._show_annotation(self._current_stored_event)
        self._show_ai_result(self._current_stored_event.ai_result)
        self.statusBar().showMessage(f"Saved annotation for event {stored.key.event_id}")

    def _save_annotation_from_controls(self) -> None:
        label = self.annotation_label_edit.text()
        note = self.annotation_note_edit.toPlainText()
        self.save_current_annotation(label=label or None, note=note or None)

    def _show_annotation(self, stored: StoredEvent | None) -> None:
        self.annotation_label_edit.setText(stored.label or "" if stored else "")
        self.annotation_note_edit.setPlainText(stored.note or "" if stored else "")
        enabled = stored is not None
        self.annotation_label_edit.setEnabled(enabled)
        self.annotation_note_edit.setEnabled(enabled)
        self.save_annotation_button.setEnabled(enabled)

    def _show_ai_result(self, result: AiResult | None) -> None:
        if result is None:
            self.ai_result_label.setText("No model result stored for this event")
            return
        if result.status == 1:
            self.ai_result_label.setText(
                "Model prediction: "
                f"class={result.class_index}/{result.class_count}, "
                f"confidence={result.confidence:.3f}, "
                f"model=v{result.model_version}, "
                f"quality=0x{result.quality_flags:02X}"
            )
        else:
            self.ai_result_label.setText(
                "Model result: "
                f"status={result.status}, failure={result.failure_reason}, "
                f"model=v{result.model_version}, "
                f"quality=0x{result.quality_flags:02X}"
            )

    def _read_ai_result(self) -> None:
        if self._session is None or self._current_stored_event is None:
            self.statusBar().showMessage("Open an indexed event before reading its model result")
            return
        key = self._current_stored_event.key
        session = self._session
        self._start_device_operation(
            lambda: (key, session.get_ai_result(key.event_id)),
            self._on_ai_result_read,
        )

    def _on_ai_result_read(self, value: object) -> None:
        key, result = value
        assert isinstance(result, AiResult)
        self._repository.update_ai_result(key, result)
        if self._current_stored_event is not None and self._current_stored_event.key == key:
            self._current_stored_event = self._repository.get_event(key)
            self._show_ai_result(self._current_stored_event.ai_result)
        self.statusBar().showMessage(f"Model result stored for event {key.event_id}")

    def _export_png(self) -> None:
        self._choose_export_target("PNG files (*.png)", self.replay_widget.export_png)

    def _export_raw(self) -> None:
        source = self._current_raw_path
        if source is None:
            return
        self._choose_export_target("Recorder events (*.terp-event)", lambda path: export_raw(source, path)[0])

    def _choose_export_target(self, file_filter: str, export: Callable[[Path], Path]) -> None:
        filename, _ = QFileDialog.getSaveFileName(self, "Export evidence", str(self._repository.exports_root), file_filter)
        if not filename:
            return
        try:
            output = export(Path(filename))
        except (OSError, ValueError) as error:
            QMessageBox.warning(self, "Export failed", str(error))
            return
        self.statusBar().showMessage(f"Exported {output.name}")

    def _required_event(self) -> DecodedEvent:
        if self._current_event is None:
            raise ValueError("no local event is open")
        return self._current_event

    def _on_local_event_loaded(self, result: object) -> None:
        raw_path, event = result
        assert isinstance(raw_path, Path)
        assert isinstance(event, DecodedEvent)
        self.show_decoded_event(event, raw_path)

    def _start_operation(self, operation: Callable[[], object], succeeded: Callable[[object], None]) -> None:
        worker = OperationWorker(operation)
        worker.succeeded.connect(succeeded)
        worker.failed.connect(self._on_worker_failed)
        self._start_worker(worker)

    def _start_device_operation(
        self,
        operation: Callable[[], object],
        succeeded: Callable[[object], None],
    ) -> None:
        worker = OperationWorker(operation)
        worker.succeeded.connect(succeeded)
        worker.failed.connect(self._on_worker_failed)
        self._start_device_worker(worker)

    def _start_device_worker(self, worker: OperationWorker | DownloadWorker) -> None:
        self._device_workers.append(worker)
        self._set_device_actions_busy(True)
        worker.finished.connect(lambda: self._finish_device_worker(worker))
        self._start_worker(worker)

    def _finish_device_worker(self, worker: OperationWorker | DownloadWorker) -> None:
        if worker in self._device_workers:
            self._device_workers.remove(worker)
        if not self._device_workers:
            self._set_device_actions_busy(False)

    def _set_device_actions_busy(self, busy: bool) -> None:
        ready = self._session is not None and self._session.state is SessionState.READY
        self.connect_action.setEnabled(not busy)
        self.connect_button.setEnabled(not busy)
        self.download_action.setEnabled(not busy and ready)
        self.download_button.setEnabled(not busy and ready)
        self.load_more_button.setEnabled(not busy and ready and self._next_event_id != 0)
        self.read_ai_result_button.setEnabled(
            not busy and ready and self._current_stored_event is not None
        )

    def _start_worker(self, worker: OperationWorker | DownloadWorker) -> None:
        self._workers.append(worker)
        worker.finished.connect(lambda: self._workers.remove(worker) if worker in self._workers else None)
        worker.start()

    def _on_worker_failed(self, error: object) -> None:
        assert isinstance(error, OperationError)
        self.statusBar().showMessage(f"{error.exception_type}: {error.message}")
