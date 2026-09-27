"""Qt worker threads that return data and errors through UI-safe signals."""

from __future__ import annotations

from dataclasses import dataclass
import threading
from typing import Callable

from PySide6.QtCore import QThread, Signal

from ..protocol.client import DownloadCancelled, DownloadProgress


@dataclass(frozen=True)
class OperationError:
    message: str
    exception_type: str

    @classmethod
    def from_exception(cls, error: Exception) -> "OperationError":
        return cls(message=str(error), exception_type=type(error).__name__)


class OperationWorker(QThread):
    """Run one blocking operation and never let its exception stay in a thread."""

    succeeded = Signal(object)
    failed = Signal(object)

    def __init__(self, operation: Callable[[], object]) -> None:
        super().__init__()
        self._operation = operation

    def run(self) -> None:
        try:
            self.succeeded.emit(self._operation())
        except Exception as error:
            self.failed.emit(OperationError.from_exception(error))


class DownloadWorker(QThread):
    """Run a download with a cancellation token and verified-byte progress signal."""

    succeeded = Signal(object)
    failed = Signal(object)
    cancelled = Signal()
    progress = Signal(object)

    def __init__(
        self,
        operation: Callable[[Callable[[], bool], Callable[[DownloadProgress], None]], object],
    ) -> None:
        super().__init__()
        self._operation = operation
        self._cancel_requested = threading.Event()

    def cancel(self) -> None:
        self._cancel_requested.set()

    def run(self) -> None:
        try:
            self.succeeded.emit(
                self._operation(self._cancel_requested.is_set, self.progress.emit)
            )
        except DownloadCancelled:
            self.cancelled.emit()
        except Exception as error:
            self.failed.emit(OperationError.from_exception(error))
