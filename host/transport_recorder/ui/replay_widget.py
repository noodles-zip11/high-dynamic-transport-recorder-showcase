"""Bounded PyQtGraph presentation of one decoded event."""

from __future__ import annotations

from pathlib import Path

import pyqtgraph as pg
from pyqtgraph.exporters import ImageExporter
from PySide6.QtWidgets import QLabel, QVBoxLayout, QWidget

from ..analysis.decimation import min_max_envelope
from ..analysis.event_record import DecodedEvent


class ReplayWidget(QWidget):
    """Render raw-count channels while preserving extrema in dense events."""

    def __init__(self) -> None:
        super().__init__()
        self._event: DecodedEvent | None = None
        self._event_id = QLabel("No local event selected")
        self._warnings = QLabel()
        self._cursor = QLabel("Cursor: no event")
        self._graph = pg.GraphicsLayoutWidget()
        self._acceleration_plot = self._graph.addPlot(row=0, col=0)
        self._angular_rate_plot = self._graph.addPlot(row=1, col=0)
        self._configure_plots()

        layout = QVBoxLayout(self)
        layout.addWidget(self._event_id)
        layout.addWidget(self._warnings)
        layout.addWidget(self._cursor)
        layout.addWidget(self._graph, stretch=1)
        self._graph.scene().sigMouseMoved.connect(self._on_mouse_moved)

    def set_event(self, event: DecodedEvent) -> None:
        self._event = event
        self._event_id.setText(f"Event {event.metadata.event_id} (EV{event.metadata.format_version:02d})")
        self._warnings.setText(self._warning_text(event))
        self._acceleration_plot.clear()
        self._angular_rate_plot.clear()

        times = event.timestamps_us.astype(float) / 1_000_000.0
        colors = ("#d62728", "#2ca02c", "#1f77b4")
        for index, name in enumerate(("X", "Y", "Z")):
            envelope = min_max_envelope(times, event.accel_counts[:, index], maximum_points=20_000)
            self._acceleration_plot.plot(envelope.timestamps_us, envelope.values, pen=colors[index], name=name)
        magnitude = min_max_envelope(times, event.accel_magnitude_counts, maximum_points=20_000)
        self._acceleration_plot.plot(
            magnitude.timestamps_us, magnitude.values, pen="#111111", name="|a|"
        )
        for index, name in enumerate(("X", "Y", "Z")):
            envelope = min_max_envelope(times, event.gyro_counts[:, index], maximum_points=20_000)
            self._angular_rate_plot.plot(envelope.timestamps_us, envelope.values, pen=colors[index], name=name)

        trigger_time = event.timestamps_us[event.trigger_index] / 1_000_000.0
        self._acceleration_plot.addItem(pg.InfiniteLine(trigger_time, angle=90, pen="#ff7f0e"))
        self._angular_rate_plot.addItem(pg.InfiniteLine(trigger_time, angle=90, pen="#ff7f0e"))
        self._acceleration_cursor = pg.InfiniteLine(0.0, angle=90, pen="#666666")
        self._angular_rate_cursor = pg.InfiniteLine(0.0, angle=90, pen="#666666")
        self._acceleration_plot.addItem(self._acceleration_cursor)
        self._angular_rate_plot.addItem(self._angular_rate_cursor)
        self.set_cursor_time(float(times[0]))

    def event_id_text(self) -> str:
        if self._event is None:
            return ""
        return str(self._event.metadata.event_id)

    def set_cursor_time(self, seconds: float) -> None:
        """Move both plots and report every channel at one nearest event sample."""

        if self._event is None:
            return
        sample_times = self._event.timestamps_us.astype(float) / 1_000_000.0
        index = int(sample_times.searchsorted(seconds))
        index = min(max(index, 0), self._event.sample_count - 1)
        if index and abs(sample_times[index - 1] - seconds) < abs(sample_times[index] - seconds):
            index -= 1
        time_seconds = float(sample_times[index])
        self._acceleration_cursor.setValue(time_seconds)
        self._angular_rate_cursor.setValue(time_seconds)
        accel = self._event.accel_counts[index]
        gyro = self._event.gyro_counts[index]
        self._cursor.setText(
            "Cursor: "
            f"t={time_seconds:.6f} s | "
            f"ax={int(accel[0])}, ay={int(accel[1])}, az={int(accel[2])} | "
            f"gx={int(gyro[0])}, gy={int(gyro[1])}, gz={int(gyro[2])}"
        )

    def cursor_text(self) -> str:
        return self._cursor.text()

    def warning_text(self) -> str:
        return self._warnings.text()

    def export_png(self, target: Path) -> Path:
        if self._event is None:
            raise ValueError("no event is open for PNG export")
        target = Path(target)
        if target.exists():
            raise FileExistsError(f"export target already exists: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        ImageExporter(self._graph.ci).export(str(target))
        return target

    def _configure_plots(self) -> None:
        self._acceleration_plot.setLabel("left", "Acceleration", units="count")
        self._acceleration_plot.setLabel("bottom", "Event time", units="s")
        self._acceleration_plot.addLegend()
        self._angular_rate_plot.setLabel("left", "Angular rate", units="count")
        self._angular_rate_plot.setLabel("bottom", "Event time", units="s")
        self._angular_rate_plot.addLegend()
        self._angular_rate_plot.setXLink(self._acceleration_plot)
        self._graph.ci.layout.setRowStretchFactor(0, 1)
        self._graph.ci.layout.setRowStretchFactor(1, 1)

    def _on_mouse_moved(self, scene_position) -> None:
        if self._event is None:
            return
        if not self._acceleration_plot.sceneBoundingRect().contains(scene_position):
            return
        data_point = self._acceleration_plot.vb.mapSceneToView(scene_position)
        self.set_cursor_time(float(data_point.x()))

    @staticmethod
    def _warning_text(event: DecodedEvent) -> str:
        warnings: list[str] = []
        if event.metadata.lost_sample_count:
            warnings.append(
                "DATA LOSS: "
                f"{event.metadata.lost_sample_count} samples in "
                f"{event.metadata.loss_episode_count} episode(s)"
            )
        if bool(
            ((event.accel_counts == 32767) | (event.accel_counts == -32768)).any()
            or ((event.gyro_counts == 32767) | (event.gyro_counts == -32768)).any()
        ):
            warnings.append("SATURATED SAMPLE: one or more IMU axes reached int16 limits")
        if not event.metadata.utc_valid:
            warnings.append("UTC INVALID: absolute event time is unavailable")
        if event.metadata.context_valid_flags & 0x0002 == 0:
            warnings.append("ENVIRONMENT SNAPSHOT UNAVAILABLE")
        if event.metadata.storage_error_count:
            warnings.append(
                f"STORAGE ERRORS: {event.metadata.storage_error_count} recorded"
            )
        return " | ".join(warnings) if warnings else "Event validation: complete"
