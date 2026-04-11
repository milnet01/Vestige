"""Bridge between AuditRunner and the web UI — threading, events, log capture."""

from __future__ import annotations

import logging
import queue
import threading
import time
from typing import Any

import sys
from pathlib import Path

# Ensure lib is importable
AUDIT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(AUDIT_ROOT))

from lib.config import load_config
from lib.runner import AuditRunner


class QueueLogHandler(logging.Handler):
    """Captures log records and pushes them to an event queue."""

    def __init__(self, event_queue: queue.Queue):
        super().__init__()
        self._queue = event_queue

    def emit(self, record: logging.LogRecord) -> None:
        try:
            self._queue.put({
                "type": "log",
                "level": record.levelname.lower(),
                "message": self.format(record),
                "timestamp": time.time(),
            })
        except Exception:
            pass


class AuditSession:
    """Manages a single audit run with event streaming."""

    def __init__(self) -> None:
        self.event_queue: queue.Queue = queue.Queue(maxsize=1000)
        self._thread: threading.Thread | None = None
        self._cancel_flag = threading.Event()
        self._log_handler: QueueLogHandler | None = None
        self.results: dict | None = None
        self.running = False
        self.error: str | None = None
        self._lock = threading.Lock()

    def start(
        self,
        config_path: str | None,
        project_root: str | None,
        tiers: list[int] | None = None,
        base_ref: str | None = None,
        verbose: bool = False,
    ) -> None:
        """Start an audit in a background thread."""
        with self._lock:
            if self.running:
                raise RuntimeError("Audit already running")
            self.running = True

        self._cancel_flag.clear()
        self.results = None
        self.error = None

        # Drain any old events
        while not self.event_queue.empty():
            try:
                self.event_queue.get_nowait()
            except queue.Empty:
                break

        def _run() -> None:
            # Setup log capture
            self._log_handler = QueueLogHandler(self.event_queue)
            self._log_handler.setFormatter(
                logging.Formatter("%(asctime)s [%(levelname)s] %(message)s", "%H:%M:%S")
            )
            audit_logger = logging.getLogger("audit")
            audit_logger.addHandler(self._log_handler)

            try:
                config = load_config(config_path, project_root=project_root)

                if tiers:
                    config.raw["tiers"] = tiers
                if base_ref:
                    config.raw["changes"]["base_ref"] = base_ref

                runner = AuditRunner(
                    config,
                    verbose=verbose,
                    progress_callback=self._on_progress,
                )
                result_obj = runner.run(cancel_event=self._cancel_flag)

                # Build the markdown report
                runner.build_report(result_obj)

                self.results = result_obj.to_dict()
                self.event_queue.put({
                    "type": "complete",
                    "results": self.results,
                    "timestamp": time.time(),
                })

            except Exception as e:
                self.error = str(e)
                self.event_queue.put({
                    "type": "error",
                    "message": str(e),
                    "timestamp": time.time(),
                })
            finally:
                with self._lock:
                    self.running = False
                audit_logger.removeHandler(self._log_handler)

        self._thread = threading.Thread(target=_run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        """Cancel a running audit (cooperative — stops between tiers)."""
        self._cancel_flag.set()

    def _on_progress(self, event_type: str, data: dict[str, Any]) -> None:
        """Called by AuditRunner on each progress event."""
        data["type"] = event_type
        data["timestamp"] = time.time()
        try:
            self.event_queue.put_nowait(data)
        except queue.Full:
            pass

    def get_status(self) -> dict[str, Any]:
        """Return current session state."""
        return {
            "running": self.running,
            "results": self.results,
            "error": self.error,
        }
