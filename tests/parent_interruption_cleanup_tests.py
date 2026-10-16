#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fault-injected observer cleanup, not a claim of native Windows execution."""
import ctypes
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

from parent_observation_timing import ObservationTimeline

spec = importlib.util.spec_from_file_location(
    "windows_interruption_observer", Path(__file__).with_name("stretch_parent_interruption_windows.py"))
observer = importlib.util.module_from_spec(spec)
with patch.object(ctypes, "WinDLL", return_value=Mock(), create=True):
    spec.loader.exec_module(observer)


class OwnedProcess:
    def __init__(self, name, calls, initial=None, stop_error=None, terminal_error=False):
        self.name, self.calls, self.exit = name, calls, initial
        self.stop_error, self.terminal_error = stop_error, terminal_error
        self.closed = False

    def terminal(self):
        self.calls.append(self.name + ":terminal")
        if self.terminal_error:
            raise OSError("terminal observation failed")
        return None if self.exit is None else {"pid": self.name, "creationFileTime": 99, "exitCode": self.exit}

    def request_termination(self, code):
        self.calls.append(self.name + ":request")
        self.exit = 1 if self.stop_error else code
        if self.stop_error:
            raise self.stop_error

    def close(self):
        self.calls.append(self.name + ":close")
        self.closed = True


class CleanupTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.calls = []
        self.launcher = Mock()
        self.launcher.poll.return_value = 0
        self.log = io.BytesIO()

    def clean(self, parent, child, primary=None, frozen=None):
        return observer.cleanup_case(self.root, parent, child, frozen, self.launcher,
                                     self.log, ObservationTimeline(), "partial-held", primary)

    def test_parent_error_does_not_skip_child_or_mask_original(self):
        denied = PermissionError("termination raced with exit")
        denied.winerror = 5
        parent = OwnedProcess("parent", self.calls, stop_error=denied)
        child = OwnedProcess("helper", self.calls)
        original = AssertionError("original suspension failed")
        try:
            try:
                raise original
            finally:
                report = self.clean(parent, child, original)
        except AssertionError as received:
            self.assertIs(received, original)
        else:
            self.fail("Original failure was lost")
        self.assertTrue(parent.closed and child.closed and self.log.closed)
        self.assertIn("helper:request", self.calls)
        self.assertEqual(report["terminalObservations"][0]["exitCode"], 1)
        self.assertEqual(report["terminalObservations"][1]["exitCode"], observer.HELPER_EXIT)
        self.assertEqual(report["errors"][0]["winerror"], 5)
        self.assertTrue(report["allOwnedTerminalsObserved"])
        self.assertIn("parent-request-termination", original.__notes__[0])
        self.assertEqual(json.loads((self.root / "cleanup-terminal.json").read_text()), report["terminalObservations"])

    def test_cleanup_only_failure_remains_failed(self):
        parent = OwnedProcess("parent", self.calls, stop_error=OSError("stop failed"))
        child = OwnedProcess("helper", self.calls)
        with self.assertRaisesRegex(RuntimeError, "parent-request-termination"):
            self.clean(parent, child)
        self.assertTrue(child.closed and parent.closed)
        self.assertFalse(json.loads((self.root / "cleanup-report.json").read_text())["primaryFailurePresent"])

    def test_already_terminal_is_not_killed(self):
        parent = OwnedProcess("parent", self.calls, initial=71)
        child = OwnedProcess("helper", self.calls, initial=72)
        report = self.clean(parent, child)
        self.assertFalse(report["errors"])
        self.assertFalse(any(c.endswith(":request") for c in self.calls))
        self.assertEqual([r["exitCode"] for r in report["terminalObservations"]], [71, 72])
        self.launcher.kill.assert_not_called()

    def test_resume_and_terminal_observation_failures_still_retire_both(self):
        parent = OwnedProcess("parent", self.calls, terminal_error=True)
        child = OwnedProcess("helper", self.calls)
        frozen = Mock()
        frozen.resume.side_effect = OSError("resume failed")
        original = RuntimeError("primary")
        report = self.clean(parent, child, original, frozen)
        self.assertTrue(parent.closed and child.closed)
        self.assertIn("parent:request", self.calls)
        self.assertIn("helper:request", self.calls)
        self.launcher.kill.assert_not_called()  # Do not duplicate a successful request.
        self.launcher.wait.assert_called_once_with(timeout=5)
        self.assertFalse(report["allOwnedTerminalsObserved"])
        self.assertEqual(len(report["terminalObservations"]), 1)
        self.assertIn("resume", [e["operation"] for e in report["errors"]])
        self.assertTrue((self.root / "observation-timing.json").is_file())

    def test_launcher_fallback_when_identity_was_not_acquired(self):
        self.launcher.poll.side_effect = OSError("poll failed")
        original = RuntimeError("identity failed")
        report = self.clean(None, None, original)
        self.launcher.kill.assert_called_once()
        self.launcher.wait.assert_called_once_with(timeout=5)
        self.assertEqual(report["errors"][0]["operation"], "launcher-poll")
        self.assertTrue(self.log.closed)

    def test_evidence_write_failure_does_not_mask_original(self):
        (self.root / "cleanup-terminal.json").mkdir()
        original = AssertionError("primary")
        report = self.clean(None, None, original)
        self.assertEqual(report["errors"][0]["operation"], "terminal-report")
        self.assertTrue((self.root / "cleanup-report.json").is_file())
        self.assertIn("terminal-report", original.__notes__[0])

    def test_resume_attempts_and_closes_every_saved_thread(self):
        frozen = observer.FrozenThreads.__new__(observer.FrozenThreads)
        frozen.process = Mock()
        frozen.process.running.return_value = True
        frozen.handles = [(101, 0, 201), (102, 0, 202)]
        with patch.object(observer, "Resume", side_effect=[0xffffffff, 1]) as resume, \
             patch.object(observer, "CloseHandle", return_value=1) as close:
            with self.assertRaises(AssertionError):
                frozen.resume()
            self.assertEqual([c.args[0] for c in resume.call_args_list], [102, 101])
            self.assertEqual([c.args[0] for c in close.call_args_list], [102, 101])
        self.assertEqual(frozen.handles, [])

    def test_qualification_kill_still_requires_requested_exit(self):
        process = observer.Process.__new__(observer.Process)
        process.request_termination = Mock()
        process.terminal = Mock(return_value={"exitCode": 1})
        with self.assertRaises(AssertionError):
            process.kill(observer.PARENT_EXIT)
        process.request_termination.assert_called_once_with(observer.PARENT_EXIT)


if __name__ == "__main__":
    unittest.main(verbosity=2)
