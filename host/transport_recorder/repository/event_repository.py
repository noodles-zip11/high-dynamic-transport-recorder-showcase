"""SQLite index for validated raw event files without mutating their bytes."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import sqlite3

from ..analysis.event_record import DecodedEvent, EventMetadata, load_event
from ..protocol.client import AiResult


SCHEMA_VERSION = 2


class ValidationState(str, Enum):
    VALID = "valid"
    MISSING = "missing"


@dataclass(frozen=True)
class EventKey:
    device_serial: str
    event_id: int


@dataclass(frozen=True)
class StoredEvent:
    key: EventKey
    path: Path
    sha256: str
    validation_state: ValidationState
    metadata: EventMetadata
    label: str | None
    note: str | None
    ai_result: AiResult | None


class EventRepository:
    """Own the validated-file-to-index transition for one local data root."""

    def __init__(self, data_root: Path) -> None:
        self._data_root = Path(data_root)
        self._events_root = self._data_root / "events"
        self._database_path = self._data_root / "recorder.db"
        self._data_root.mkdir(parents=True, exist_ok=True)
        self._migrate()

    @property
    def exports_root(self) -> Path:
        path = self._data_root / "exports"
        path.mkdir(parents=True, exist_ok=True)
        return path

    @property
    def downloads_root(self) -> Path:
        path = self._data_root / "downloads"
        path.mkdir(parents=True, exist_ok=True)
        return path

    def import_event(self, source: Path, *, device_serial: str) -> StoredEvent:
        decoded = load_event(source)
        digest = _file_sha256(source)
        key = EventKey(device_serial=device_serial, event_id=decoded.metadata.event_id)
        target = self._event_path(key)
        created_target = False

        if target.exists():
            if _file_sha256(target) != digest:
                raise FileExistsError(f"event path already has different bytes: {target}")
        else:
            self._copy_atomically(source, target)
            created_target = True

        try:
            with self._connection() as connection:
                self._insert_record(connection, key, target, digest, decoded)
        except Exception:
            if created_target:
                target.unlink(missing_ok=True)
            raise
        return self.get_event(key)

    def list_events(self) -> list[StoredEvent]:
        with self._connection() as connection:
            rows = connection.execute(
                _SELECT_EVENTS + " ORDER BY events.device_serial, events.event_id"
            ).fetchall()
        return [self._row_to_event(row) for row in rows]

    def get_event(self, key: EventKey) -> StoredEvent:
        with self._connection() as connection:
            row = connection.execute(
                _SELECT_EVENTS + " WHERE events.device_serial = ? AND events.event_id = ?",
                (key.device_serial, key.event_id),
            ).fetchone()
            if row is None:
                raise KeyError(key)
            if not Path(row[2]).exists() and row[4] != ValidationState.MISSING.value:
                connection.execute(
                    "UPDATE events SET validation_state = ? WHERE device_serial = ? AND event_id = ?",
                    (ValidationState.MISSING.value, key.device_serial, key.event_id),
                )
                row = connection.execute(
                    _SELECT_EVENTS + " WHERE events.device_serial = ? AND events.event_id = ?",
                    (key.device_serial, key.event_id),
                ).fetchone()
        assert row is not None
        return self._row_to_event(row)

    def find_event_by_path(self, path: Path) -> StoredEvent | None:
        """Return the indexed event for a repository-owned raw path, if any."""

        requested = Path(path).resolve()
        with self._connection() as connection:
            row = connection.execute(
                "SELECT device_serial, event_id FROM events WHERE path IN (?, ?) LIMIT 1",
                (str(path), str(requested)),
            ).fetchone()
            if row is None:
                # Preserve databases created from a relative data root before paths
                # were opened through an absolute file-picker result.
                rows = connection.execute(
                    "SELECT device_serial, event_id, path FROM events"
                ).fetchall()
                row = next(
                    (candidate for candidate in rows
                     if Path(str(candidate[2])).resolve() == requested),
                    None,
                )
        if row is not None:
            return self.get_event(
                EventKey(device_serial=str(row[0]), event_id=int(row[1]))
            )
        return None

    def update_annotation(self, key: EventKey, *, label: str | None, note: str | None) -> None:
        self.get_event(key)
        with self._connection() as connection:
            connection.execute(
                """
                INSERT INTO annotations (device_serial, event_id, label, note)
                VALUES (?, ?, ?, ?)
                ON CONFLICT(device_serial, event_id) DO UPDATE SET
                    label = excluded.label,
                    note = excluded.note
                """,
                (key.device_serial, key.event_id, label, note),
            )

    def update_ai_result(self, key: EventKey, result: AiResult) -> None:
        """Persist a device model result independently from human annotation."""

        self.get_event(key)
        if result.event_id != key.event_id:
            raise ValueError("AI result event identity does not match repository key")
        with self._connection() as connection:
            connection.execute(
                """
                INSERT INTO ai_results (device_serial, event_id, result_json)
                VALUES (?, ?, ?)
                ON CONFLICT(device_serial, event_id) DO UPDATE SET
                    result_json = excluded.result_json
                """,
                (key.device_serial, key.event_id,
                 json.dumps(asdict(result), sort_keys=True)),
            )

    def _insert_record(
        self,
        connection: sqlite3.Connection,
        key: EventKey,
        path: Path,
        digest: str,
        decoded: DecodedEvent,
    ) -> None:
        connection.execute(
            """
            INSERT INTO events (
                device_serial, event_id, path, sha256, validation_state, metadata_json
            ) VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(device_serial, event_id) DO UPDATE SET
                path = excluded.path,
                sha256 = excluded.sha256,
                validation_state = excluded.validation_state,
                metadata_json = excluded.metadata_json
            """,
            (
                key.device_serial,
                key.event_id,
                str(path),
                digest,
                ValidationState.VALID.value,
                json.dumps(asdict(decoded.metadata), sort_keys=True),
            ),
        )

    def _event_path(self, key: EventKey) -> Path:
        return self._events_root / key.device_serial / f"{key.event_id}.terp-event"

    def _copy_atomically(self, source: Path, target: Path) -> None:
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".new")
        try:
            shutil.copyfile(source, temporary)
            os.replace(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)

    def _connection(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self._database_path)
        connection.execute("PRAGMA foreign_keys = ON")
        return connection

    def _migrate(self) -> None:
        with self._connection() as connection:
            connection.execute(
                "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)"
            )
            row = connection.execute("SELECT version FROM schema_version").fetchone()
            upgrade_from_v1 = False
            if row is None:
                connection.execute("INSERT INTO schema_version (version) VALUES (?)", (SCHEMA_VERSION,))
            elif row[0] == 1 and SCHEMA_VERSION == 2:
                upgrade_from_v1 = True
            elif row[0] != SCHEMA_VERSION:
                raise RuntimeError(f"unsupported recorder database schema {row[0]}")
            connection.execute(
                """
                CREATE TABLE IF NOT EXISTS events (
                    device_serial TEXT NOT NULL,
                    event_id INTEGER NOT NULL,
                    path TEXT NOT NULL,
                    sha256 TEXT NOT NULL,
                    validation_state TEXT NOT NULL,
                    metadata_json TEXT NOT NULL,
                    PRIMARY KEY (device_serial, event_id)
                )
                """
            )
            connection.execute(
                """
                CREATE TABLE IF NOT EXISTS annotations (
                    device_serial TEXT NOT NULL,
                    event_id INTEGER NOT NULL,
                    label TEXT,
                    note TEXT,
                    PRIMARY KEY (device_serial, event_id),
                    FOREIGN KEY (device_serial, event_id)
                        REFERENCES events (device_serial, event_id)
                )
                """
            )
            connection.execute(
                "CREATE INDEX IF NOT EXISTS events_path_index ON events (path)"
            )
            connection.execute(
                """
                CREATE TABLE IF NOT EXISTS ai_results (
                    device_serial TEXT NOT NULL,
                    event_id INTEGER NOT NULL,
                    result_json TEXT NOT NULL,
                    PRIMARY KEY (device_serial, event_id),
                    FOREIGN KEY (device_serial, event_id)
                        REFERENCES events (device_serial, event_id)
                )
                """
            )
            if upgrade_from_v1:
                connection.execute(
                    "UPDATE schema_version SET version = ?", (SCHEMA_VERSION,)
                )

    @staticmethod
    def _row_to_event(row: tuple[object, ...]) -> StoredEvent:
        metadata = EventMetadata(**json.loads(str(row[5])))
        ai_result = None
        if row[8] is not None:
            ai_result_data = json.loads(str(row[8]))
            ai_result_data["logits"] = tuple(ai_result_data["logits"])
            ai_result = AiResult(**ai_result_data)
        return StoredEvent(
            key=EventKey(device_serial=str(row[0]), event_id=int(row[1])),
            path=Path(str(row[2])),
            sha256=str(row[3]),
            validation_state=ValidationState(str(row[4])),
            metadata=metadata,
            label=None if row[6] is None else str(row[6]),
            note=None if row[7] is None else str(row[7]),
            ai_result=ai_result,
        )


_SELECT_EVENTS = """
SELECT events.device_serial, events.event_id, events.path, events.sha256,
       events.validation_state, events.metadata_json, annotations.label, annotations.note,
       ai_results.result_json
FROM events
LEFT JOIN annotations
    ON annotations.device_serial = events.device_serial
   AND annotations.event_id = events.event_id
LEFT JOIN ai_results
    ON ai_results.device_serial = events.device_serial
   AND ai_results.event_id = events.event_id
"""


def _file_sha256(path: Path) -> str:
    digest = sha256()
    with path.open("rb") as event_file:
        while chunk := event_file.read(64 * 1024):
            digest.update(chunk)
    return digest.hexdigest()
