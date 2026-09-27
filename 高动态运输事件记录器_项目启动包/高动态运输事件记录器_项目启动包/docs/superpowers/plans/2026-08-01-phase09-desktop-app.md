# Phase 09 Desktop Application and Event Replay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an offline-first PySide6 desktop application that downloads, indexes, replays and exports immutable recorder events through the existing Python TERP library.

**Architecture:** Keep TERP codec/client behavior intact. Add pure-Python analysis and repository layers that can be tested without Qt or hardware; add a cancellable device-session facade over the existing byte transport; keep Qt widgets as signal-driven consumers of immutable view data. All file and database transitions are explicit: validated raw bytes first, then a SQLite index transaction.

**Tech Stack:** Python 3.12, PySide6, PyQtGraph, NumPy, pyserial, SQLite, pytest, pytest-qt.

---

## File Structure

| Path | Responsibility |
|---|---|
| `pyproject.toml`, `requirements-dev-lock.txt` | Runtime and test dependencies for the desktop application. |
| `host/transport_recorder/analysis/event_record.py` | Strict EV02 header/payload validation and immutable decoded event models. |
| `host/transport_recorder/analysis/decimation.py` | Peak-preserving min/max envelope data for bounded plots. |
| `host/transport_recorder/analysis/export.py` | CSV, JSON, PNG and raw-byte export helpers. |
| `host/transport_recorder/repository/event_repository.py` | Versioned SQLite schema, raw-file import and annotation access. |
| `host/transport_recorder/device/session.py` | Cancellable session operations that reuse `TerpClient` and `SerialTransport`. |
| `host/transport_recorder/ui/workers.py` | QThread operation worker wrapper and UI-safe signals. |
| `host/transport_recorder/ui/main_window.py` | Main window, navigation, status and action state. |
| `host/transport_recorder/ui/replay_widget.py` | Replay presentation backed by decimated channels. |
| `host/transport_recorder/app.py` | Application entry point and dependency composition. |
| `host/tests/fixtures/events/` | EV02 fixture files: valid, CRC-invalid, truncated and unknown-version. |
| `host/tests/unit/` | Pure parser, decimation, repository and export tests. |
| `host/tests/integration/` | Simulated TERP/session/download-to-repository tests. |
| `host/tests/ui/` | `pytest-qt` widget, state-change and worker-error tests. |

`START_LIVE`, `STOP_LIVE` and `DELETE_EVENT` are deliberately absent. The
desktop app must show deletion as unsupported rather than call `TerpClient.delete_event()`.

### Task 1: Add dependency and test-layout support

**Files:**
- Modify: `pyproject.toml`
- Modify: `requirements-dev-lock.txt`
- Create: `host/tests/unit/__init__.py`
- Create: `host/tests/integration/__init__.py`
- Create: `host/tests/ui/__init__.py`
- Test: `host/tests/test_environment_smoke.py`

- [ ] **Step 1: Write the failing dependency smoke test.**

```python
def test_desktop_dependencies_are_importable() -> None:
    import numpy
    import pyqtgraph
    import PySide6

    assert numpy.__version__
    assert pyqtgraph.__version__
    assert PySide6.__version__
```

- [ ] **Step 2: Run the focused test and verify the missing-module failure.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/test_environment_smoke.py::test_desktop_dependencies_are_importable -q`

Expected: FAIL because PySide6 and PyQtGraph are not installed.

- [ ] **Step 3: Add the supported desktop dependencies.**

Keep `pyserial==3.5`; add compatible pinned entries for `PySide6`, `pyqtgraph`,
`numpy` and `pytest-qt` to the lock file and corresponding runtime/test
requirements to `pyproject.toml`. Use package versions installed from that
lock file, not an implicit machine-global dependency.

```toml
[project]
dependencies = [
    "pyserial==3.5",
    "PySide6==6.11.1",
    "pyqtgraph==0.14.0",
    "numpy==2.5.1",
]

[project.optional-dependencies]
test = ["pytest-qt==4.5.0"]
```

- [ ] **Step 4: Install the locked requirements and verify the focused test passes.**

Run: `./.venv/Scripts/python.exe -m pip install -r requirements-dev-lock.txt`

Run: `./.venv/Scripts/python.exe -m pytest host/tests/test_environment_smoke.py::test_desktop_dependencies_are_importable -q`

Expected: PASS.

### Task 2: Establish valid and invalid EV02 event fixtures

**Files:**
- Create: `host/tests/fixtures/events/valid-42.terp-event`
- Create: `host/tests/fixtures/events/crc-invalid-42.terp-event`
- Create: `host/tests/fixtures/events/truncated-42.terp-event`
- Create: `host/tests/fixtures/events/unknown-version-42.terp-event`
- Create: `host/tests/unit/conftest.py`
- Test: `host/tests/unit/test_event_record.py`

- [ ] **Step 1: Write a failing parser test around the four fixture outcomes.**

```python
def test_load_event_accepts_a_valid_ev02_fixture(fixture_events: Path) -> None:
    event = load_event(fixture_events / "valid-42.terp-event")
    assert event.metadata.event_id == 42
    assert event.sample_count == 4

@pytest.mark.parametrize(
    ("name", "error"),
    [
        ("crc-invalid-42.terp-event", "payload CRC"),
        ("truncated-42.terp-event", "record length"),
        ("unknown-version-42.terp-event", "unsupported event version"),
    ],
)
def test_load_event_rejects_invalid_fixture(
    fixture_events: Path, name: str, error: str
) -> None:
    with pytest.raises(EventFormatError, match=error):
        load_event(fixture_events / name)
```

- [ ] **Step 2: Run the test and verify it fails because the analysis module is absent.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/unit/test_event_record.py -q`

Expected: collection failure for `transport_recorder.analysis.event_record`.

- [ ] **Step 3: Add a deterministic fixture builder and materialize the four files.**

The helper must create a 128-byte EV02 header and four 16-byte samples using
`struct.pack`. It must set `event_id=42`, `sample_rate_hz=1600`, one
pretrigger sample, three posttrigger samples and a correct IEEE CRC-32 over
the payload. Copy the valid bytes and change only the requested condition for
each invalid file: payload CRC, final length or format-version field.

```python
SAMPLE = struct.Struct("<hhhhhhHbB")

def write_ev02_fixture(path: Path, *, version: int = 2,
                       payload_crc_delta: int = 0,
                       truncate_bytes: int = 0) -> None:
    payload = b"".join(SAMPLE.pack(*sample) for sample in SAMPLES)
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    header = bytearray(128)
    struct.pack_into("<4sHHI", header, 0, b"EV02", version, 128, 42)
    struct.pack_into(
        "<QqIIIIIHHIIII", header, 12,
        1_000_000, 1_700_000_000, 1, 1600, 77, 1, 3, 0, 0,
        9_409, 4_096, len(payload), checksum ^ payload_crc_delta,
    )
    path.write_bytes(bytes(header) + payload[:-truncate_bytes or None])
```

- [ ] **Step 4: Run the tests again and verify they now reach the missing parser.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/unit/test_event_record.py -q`

Expected: FAIL on missing `load_event`, not fixture creation.

### Task 3: Implement strict event parsing and peak-preserving replay data

**Files:**
- Create: `host/transport_recorder/analysis/__init__.py`
- Create: `host/transport_recorder/analysis/event_record.py`
- Create: `host/transport_recorder/analysis/decimation.py`
- Test: `host/tests/unit/test_event_record.py`
- Test: `host/tests/unit/test_decimation.py`

- [ ] **Step 1: Write the failing decimation test.**

```python
def test_min_max_envelope_retains_an_impulse_between_bucket_edges() -> None:
    envelope = min_max_envelope(
        numpy.array([0, 0, 97, 0, 0, 0], dtype=numpy.int16), maximum_points=4
    )
    assert 97 in envelope.values
    assert len(envelope.timestamps_us) == len(envelope.values)
```

- [ ] **Step 2: Verify the test is red.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/unit/test_decimation.py -q`

Expected: collection failure because `min_max_envelope` does not exist.

- [ ] **Step 3: Implement immutable decoded models and validation.**

`load_event()` must check magic, version, header length, reserved context bits,
payload length, complete record length and payload CRC before constructing
`EventMetadata` and `DecodedEvent`. Decode the sixteen-byte sample layout as
six signed 16-bit axes, unsigned 16-bit device timestamp, signed temperature
and unsigned FIFO header. Generate the common time axis from sample rate,
with trigger index equal to `pretrigger_samples`.

```python
@dataclass(frozen=True)
class DecodedEvent:
    metadata: EventMetadata
    accel_counts: numpy.ndarray
    gyro_counts: numpy.ndarray
    timestamps_us: numpy.ndarray

    @property
    def sample_count(self) -> int:
        return int(self.accel_counts.shape[0])

def load_event(path: Path) -> DecodedEvent:
    raw = path.read_bytes()
    if len(raw) < 128:
        raise EventFormatError("record length is shorter than EV02 header")
    # Validate the fixed header and CRC before NumPy views are created.
```

`min_max_envelope()` must return every source vector unchanged when it already
fits the requested point count. Otherwise, split the input into contiguous
buckets and emit each bucket's minimum and maximum in time order. Do not use
plain stride slicing.

- [ ] **Step 4: Verify parser and decimation tests are green.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/unit/test_event_record.py host/tests/unit/test_decimation.py -q`

Expected: PASS.

### Task 4: Implement atomic local repository and annotations

**Files:**
- Create: `host/transport_recorder/repository/__init__.py`
- Create: `host/transport_recorder/repository/models.py`
- Create: `host/transport_recorder/repository/event_repository.py`
- Test: `host/tests/unit/test_event_repository.py`

- [ ] **Step 1: Write failing repository tests for index insertion, rollback and missing files.**

```python
def test_import_validated_event_places_bytes_before_committing_index(tmp_path: Path) -> None:
    repository = EventRepository(tmp_path)
    record = repository.import_event(valid_fixture, device_serial="SIM-0001")
    assert record.validation_state is ValidationState.VALID
    assert record.path.read_bytes() == valid_fixture.read_bytes()

def test_failed_index_transaction_leaves_no_valid_record(tmp_path: Path, monkeypatch) -> None:
    repository = EventRepository(tmp_path)
    monkeypatch.setattr(repository, "_insert_record", lambda *_: (_ for _ in ()).throw(sqlite3.Error()))
    with pytest.raises(sqlite3.Error):
        repository.import_event(valid_fixture, device_serial="SIM-0001")
    assert repository.list_events() == []

def test_missing_raw_file_is_reported_without_deleting_annotation(tmp_path: Path) -> None:
    repository = EventRepository(tmp_path)
    record = repository.import_event(valid_fixture, device_serial="SIM-0001")
    repository.update_annotation(record.key, label="impact", note="right side")
    record.path.unlink()
    assert repository.get_event(record.key).validation_state is ValidationState.MISSING
```

- [ ] **Step 2: Verify the repository test is red.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/unit/test_event_repository.py -q`

Expected: collection failure because `EventRepository` does not exist.

- [ ] **Step 3: Implement versioned schema and the file-before-index transition.**

Use paths such as `data/events/SIM-0001/42.terp-event`, `data/exports/` and
`data/recorder.db` below the caller-supplied root. Schema migration version 1
must contain `schema_version`, `events` and `annotations`. Raw-file SHA-256
is calculated from bytes. `import_event()` validates with `load_event()`,
copies to a temporary sibling, calls `os.replace()`, then begins the SQLite
transaction. If insertion fails, remove only the just-created final file and
leave unrelated raw files untouched.

```python
def import_event(self, source: Path, *, device_serial: str) -> StoredEvent:
    decoded = load_event(source)
    target = self._event_path(device_serial, decoded.metadata.event_id)
    digest = sha256(source.read_bytes()).hexdigest()
    self._copy_atomically(source, target)
    try:
        with self._connection() as connection:
            self._insert_record(connection, target, digest, decoded, device_serial)
    except Exception:
        target.unlink(missing_ok=True)
        raise
    return self.get_event(EventKey(device_serial, decoded.metadata.event_id))
```

- [ ] **Step 4: Verify all repository tests are green.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/unit/test_event_repository.py -q`

Expected: PASS.

### Task 5: Implement export helpers with round-trip checks

**Files:**
- Create: `host/transport_recorder/analysis/export.py`
- Test: `host/tests/unit/test_export.py`

- [ ] **Step 1: Write failing export tests.**

```python
def test_csv_round_trip_preserves_sample_count_endpoints_and_peak(tmp_path: Path) -> None:
    event = load_event(valid_fixture)
    output = export_csv(event, tmp_path / "event.csv")
    rows = list(csv.DictReader(output.open(newline="", encoding="utf-8")))
    assert len(rows) == event.sample_count
    assert int(rows[0]["time_us"]) == int(event.timestamps_us[0])
    assert int(rows[-1]["time_us"]) == int(event.timestamps_us[-1])
    assert max(int(row["accel_x_count"]) for row in rows) == 97

def test_raw_export_is_byte_identical_and_writes_sha256(tmp_path: Path) -> None:
    output, digest_path = export_raw(valid_fixture, tmp_path / "raw")
    assert output.read_bytes() == valid_fixture.read_bytes()
    assert digest_path.read_text(encoding="utf-8").strip() == sha256(output.read_bytes()).hexdigest()
```

- [ ] **Step 2: Verify the focused export test is red.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/unit/test_export.py -q`

Expected: collection failure because export functions do not exist.

- [ ] **Step 3: Implement non-overwriting exports.**

CSV must contain event metadata comment rows, a header naming count units and
sample rows. JSON must contain metadata, validation information and optional
annotation data. `export_raw()` copies bytes with `shutil.copyfile` and writes
the SHA-256 in a sibling `.sha256` file. `ensure_new_path()` must raise
`FileExistsError` if the destination exists; UI asks the user before a later
explicit retry.

```python
def ensure_new_path(path: Path) -> Path:
    if path.exists():
        raise FileExistsError(f"export target already exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    return path
```

- [ ] **Step 4: Verify export tests pass.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/unit/test_export.py -q`

Expected: PASS.

### Task 6: Implement session operations over the existing TERP client

**Files:**
- Create: `host/transport_recorder/device/__init__.py`
- Create: `host/transport_recorder/device/models.py`
- Create: `host/transport_recorder/device/session.py`
- Test: `host/tests/integration/test_device_session.py`

- [ ] **Step 1: Write failing tests using `SimulatedDevice` and `MemoryTransport`.**

```python
def test_session_connects_lists_a_page_and_downloads_into_repository(tmp_path: Path) -> None:
    session = DeviceSession(lambda: MemoryTransport(SimulatedDevice({42: valid_fixture.read_bytes()})))
    connected = session.connect()
    page = session.list_events(after_event_id=0, limit=16)
    stored = session.download_to_repository(42, EventRepository(tmp_path))
    assert connected.identity.serial_number == "SIM-0001"
    assert [item.event_id for item in page.events] == [42]
    assert stored.validation_state is ValidationState.VALID

def test_cancelled_download_keeps_terp_partial_files(tmp_path: Path) -> None:
    session = DeviceSession(disconnecting_factory)
    session.connect()
    with pytest.raises(OperationCancelled):
        session.download_to_path(42, tmp_path / "42.terp-event", cancelled=lambda: True)
    assert (tmp_path / "42.terp-event.part").exists()
```

- [ ] **Step 2: Verify the session tests are red.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/integration/test_device_session.py -q`

Expected: collection failure because `DeviceSession` does not exist.

- [ ] **Step 3: Implement the session facade without changing TERP protocol code.**

`DeviceSession.connect()` opens a transport, creates `TerpClient`, calls
`hello()` and `get_health()`, and returns a frozen `ConnectedDevice` model.
`list_events()` delegates only to `TerpClient.list_events()`. Download writes
to an application staging path via existing `TerpClient.download_event()` and
only imports its completed result into `EventRepository`. The cancellation
predicate is tested between chunks through a small `CancellableTransport`
wrapper that raises `OperationCancelled` before the next read; do not alter
TERP message identifiers or client framing.

```python
@dataclass(frozen=True)
class EventPage:
    events: Sequence[EventInfo]
    next_event_id: int

class DeviceSession:
    def connect(self) -> ConnectedDevice:
        self._client = TerpClient(self._transport_factory())
        identity = self._client.hello()
        return ConnectedDevice(identity=identity, health=self._client.get_health())

    def list_events(self, *, after_event_id: int, limit: int) -> EventPage:
        events, next_event_id = self._require_client().list_events(after_event_id, limit)
        return EventPage(events=tuple(events), next_event_id=next_event_id)
```

- [ ] **Step 4: Verify the integration tests pass.**

Run: `./.venv/Scripts/python.exe -m pytest host/tests/integration/test_device_session.py -q`

Expected: PASS.

### Task 7: Add Qt worker boundary and replay-capable main window

**Files:**
- Create: `host/transport_recorder/ui/__init__.py`
- Create: `host/transport_recorder/ui/workers.py`
- Create: `host/transport_recorder/ui/replay_widget.py`
- Create: `host/transport_recorder/ui/main_window.py`
- Create: `host/transport_recorder/app.py`
- Test: `host/tests/ui/test_main_window.py`
- Test: `host/tests/ui/test_workers.py`

- [ ] **Step 1: Write failing UI and worker tests.**

```python
def test_main_window_keeps_decoded_replay_when_disconnected(qtbot, valid_fixture: Path) -> None:
    window = MainWindow(repository=EventRepository(temp_root), session_factory=fake_session_factory)
    qtbot.addWidget(window)
    window.show_decoded_event(load_event(valid_fixture))
    window.show_disconnected("cable removed")
    assert window.replay_widget.event_id_text() == "42"
    assert not window.download_action.isEnabled()

def test_worker_reports_exception_through_error_signal(qtbot) -> None:
    worker = OperationWorker(lambda: (_ for _ in ()).throw(RuntimeError("serial failed")))
    with qtbot.waitSignal(worker.failed, timeout=1000) as signal:
        worker.start()
    assert "serial failed" in signal.args[0].message
```

- [ ] **Step 2: Verify the UI tests fail before implementation.**

Run: `$env:QT_QPA_PLATFORM='offscreen'; ./.venv/Scripts/python.exe -m pytest host/tests/ui -q`

Expected: collection failure because UI modules do not exist.

- [ ] **Step 3: Implement worker signals and a minimal usable window.**

`OperationWorker` is a `QThread` that executes a callable, emits one frozen
success model or one frozen `OperationError`, and always emits a finished
state. `MainWindow` contains a left device/event navigation pane, a central
stacked connection/replay page, status bar text, `Open local event` and export
actions. It submits `load_event(path)` through `OperationWorker`; only its
success signal calls `show_decoded_event()` on the main thread. Connect,
list and download operations use the same worker boundary. The replay widget
uses PyQtGraph to show accel X/Y/Z, magnitude and gyro X/Y/Z from
`min_max_envelope()` plus a trigger line. Its labels come from decoded
metadata, not hard-coded physical range values.

```python
class OperationWorker(QThread):
    succeeded = Signal(object)
    failed = Signal(OperationError)

    def run(self) -> None:
        try:
            self.succeeded.emit(self._operation())
        except Exception as error:
            self.failed.emit(OperationError.from_exception(error))

class MainWindow(QMainWindow):
    def open_local_event(self, path: Path) -> None:
        self._start_worker(lambda: load_event(path), self.show_decoded_event)

    def show_decoded_event(self, event: DecodedEvent) -> None:
        self.replay_widget.set_event(event)
        self.statusBar().showMessage(f"Opened event {event.metadata.event_id}")

    def show_disconnected(self, reason: str) -> None:
        self.download_action.setEnabled(False)
        self.statusBar().showMessage(f"Disconnected: {reason}")
```

- [ ] **Step 4: Verify all UI tests pass offscreen.**

Run: `$env:QT_QPA_PLATFORM='offscreen'; ./.venv/Scripts/python.exe -m pytest host/tests/ui -q`

Expected: PASS.

### Task 8: Wire the app, enforce boundaries and run the acceptance suite

**Files:**
- Modify: `host/transport_recorder/__init__.py`
- Modify: `host/transport_recorder/cli.py` only if import relocation is needed
- Create: `host/tests/test_desktop_boundaries.py`
- Modify: `scripts/run_tests.ps1`
- Modify: `高动态运输事件记录器_项目启动包/高动态运输事件记录器_项目启动包/docs/superpowers/plans/transport-recorder/09_PySide6上位机与数据回放.md`

- [ ] **Step 1: Write failing layer-boundary and entry-point tests.**

```python
def test_protocol_does_not_import_qt_or_repository() -> None:
    protocol_sources = "\n".join(path.read_text(encoding="utf-8") for path in PROTOCOL_FILES)
    assert "PySide6" not in protocol_sources
    assert "repository" not in protocol_sources

def test_ui_does_not_construct_terp_frames() -> None:
    ui_sources = "\n".join(path.read_text(encoding="utf-8") for path in UI_FILES)
    assert "encode_frame(" not in ui_sources
    assert "Frame(" not in ui_sources

def test_app_can_start_without_a_connected_device(qtbot) -> None:
    window = create_main_window(data_root=temp_root)
    qtbot.addWidget(window)
    window.show()
    assert window.statusBar().currentMessage() == "No device connected"
```

- [ ] **Step 2: Verify the boundary test is red.**

Run: `$env:QT_QPA_PLATFORM='offscreen'; ./.venv/Scripts/python.exe -m pytest host/tests/test_desktop_boundaries.py -q`

Expected: FAIL until app composition and boundary-safe imports exist.

- [ ] **Step 3: Add the application entry point and test runner coverage.**

`python -m transport_recorder.app` must construct `QApplication`, create a
repository rooted at the explicit data root (or the user data directory),
show an initially disconnected `MainWindow` and start the event loop. Update
the test runner to include all three `host/tests/{unit,integration,ui}` paths
and execute UI tests with `QT_QPA_PLATFORM=offscreen`. Mark completed items in
the Phase 09 plan only when their listed verification has passed; record that
real USB, reconnect and four-hour endurance remain hardware acceptance work.

- [ ] **Step 4: Run narrow, full host and final diff verification.**

Run: `$env:QT_QPA_PLATFORM='offscreen'; ./.venv/Scripts/python.exe -m pytest host/tests/unit host/tests/integration host/tests/ui host/tests -q`

Run: `pwsh scripts/run_tests.ps1`

Run: `git diff --check`

Expected: all available automated tests pass and `git diff --check` has no
output. Report the no-device application launch separately from unperformed
USB hardware acceptance.

## Commit Boundary

Do not commit, push, merge or open a PR as part of this plan unless the user
gives explicit authorization. If authorized later, stage only Phase 09 files
and use one atomic Conventional Commit after all acceptance checks pass.
