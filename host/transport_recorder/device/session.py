"""Cancellable application-level operations over an existing TERP client."""

from __future__ import annotations

from dataclasses import dataclass
from contextlib import contextmanager
from enum import Enum
from threading import Lock, RLock
from typing import Callable

from ..protocol.client import (
    AiResult,
    ByteTransport,
    DeviceInfo,
    DownloadProgress,
    DownloadCancelled,
    EventInfo,
    HealthInfo,
    TerpClient,
)
from ..repository.event_repository import EventRepository, StoredEvent


class SessionState(str, Enum):
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    READY = "ready"
    TRANSFERRING = "transferring"
    ERROR = "error"


@dataclass(frozen=True)
class ConnectedDevice:
    identity: DeviceInfo
    health: HealthInfo


@dataclass(frozen=True)
class EventPage:
    events: tuple[EventInfo, ...]
    next_event_id: int


class DeviceSession:
    """Keep UI code out of TERP request sequencing and resumable file handling."""

    def __init__(
        self,
        transport_factory: Callable[[], ByteTransport],
        *,
        startup_timeout_seconds: float = 15.0,
    ) -> None:
        self._transport_factory = transport_factory
        self._startup_timeout_seconds = startup_timeout_seconds
        self._client: TerpClient | None = None
        self._connected: ConnectedDevice | None = None
        self._state = SessionState.DISCONNECTED
        self._state_lock = RLock()
        self._operation_lock = Lock()

    @property
    def state(self) -> SessionState:
        with self._state_lock:
            return self._state

    @property
    def connected_device(self) -> ConnectedDevice | None:
        with self._state_lock:
            return self._connected

    def connect(self) -> ConnectedDevice:
        with self._exclusive_operation():
            with self._state_lock:
                self._close_client_locked()
                self._state = SessionState.CONNECTING
            client: TerpClient | None = None
            try:
                client, identity = TerpClient.connect_with_retry(
                    self._transport_factory,
                    startup_timeout_seconds=self._startup_timeout_seconds,
                )
                connected = ConnectedDevice(identity=identity, health=client.get_health())
            except Exception:
                if client is not None:
                    client.close()
                with self._state_lock:
                    self._state = SessionState.ERROR
                raise
            with self._state_lock:
                self._client = client
                self._connected = connected
                self._state = SessionState.READY
            return connected

    def list_events(self, *, after_event_id: int, limit: int) -> EventPage:
        with self._exclusive_operation():
            with self._state_lock:
                client = self._require_ready_client_locked()
            events, next_event_id = client.list_events(after_event_id, limit)
            return EventPage(events=tuple(events), next_event_id=next_event_id)

    def get_ai_result(self, event_id: int = 0) -> AiResult:
        with self._exclusive_operation():
            with self._state_lock:
                client = self._require_ready_client_locked()
            return client.get_ai_result(event_id)

    def download_to_repository(
        self,
        event_id: int,
        repository: EventRepository,
        *,
        cancelled: Callable[[], bool] | None = None,
        progress: Callable[[DownloadProgress], None] | None = None,
    ) -> StoredEvent:
        with self._exclusive_operation():
            with self._state_lock:
                client = self._require_ready_client_locked()
                connected = self._require_connected_locked()
                self._state = SessionState.TRANSFERRING
            staging_path = repository.downloads_root / connected.identity.serial_number / (
                f"{event_id}.terp-event"
            )
            try:
                completed_path = client.download_event(
                    event_id,
                    staging_path,
                    reconnect=self._transport_factory,
                    cancelled=cancelled,
                    progress=progress,
                )
                record = repository.import_event(
                    completed_path, device_serial=connected.identity.serial_number
                )
            except DownloadCancelled:
                with self._state_lock:
                    self._state = SessionState.READY
                raise
            except Exception:
                client.close()
                with self._state_lock:
                    self._client = None
                    self._connected = None
                    self._state = SessionState.ERROR
                raise
            with self._state_lock:
                self._state = SessionState.READY
            completed_path.unlink(missing_ok=True)
            return record

    def close(self) -> None:
        with self._operation_lock:
            with self._state_lock:
                self._close_client_locked()
                self._state = SessionState.DISCONNECTED

    @contextmanager
    def _exclusive_operation(self):
        if not self._operation_lock.acquire(blocking=False):
            raise RuntimeError("device operation already in progress")
        try:
            yield
        finally:
            self._operation_lock.release()

    def _close_client_locked(self) -> None:
        if self._client is not None:
            self._client.close()
        self._client = None
        self._connected = None

    def _require_ready_client_locked(self) -> TerpClient:
        if self._client is None or self._state is not SessionState.READY:
            raise RuntimeError("device session is not connected")
        return self._client

    def _require_connected_locked(self) -> ConnectedDevice:
        if self._connected is None:
            raise RuntimeError("device session has no identity")
        return self._connected
