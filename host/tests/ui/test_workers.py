from __future__ import annotations

from host.transport_recorder.ui.workers import OperationWorker


def test_worker_reports_exception_through_error_signal(qtbot) -> None:
    def fail() -> object:
        raise RuntimeError("serial failed")

    worker = OperationWorker(fail)
    with qtbot.waitSignal(worker.failed, timeout=1_000) as signal:
        worker.start()

    assert "serial failed" in signal.args[0].message
