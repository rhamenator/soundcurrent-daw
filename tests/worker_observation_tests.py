#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual pipe/deadline failures without DSP or a VM."""
import json
import os
import signal
import sys
import tempfile
import time
import unittest
from pathlib import Path
from worker_observation import observe, diagnostic


class WorkerObservation(unittest.TestCase):
    def worker(self, code, request="{}", **options):
        return observe([sys.executable, "-u", "-c", code], request, **options)

    def test_empty_stderr_failure_retains_exit_and_packet(self):
        result = self.worker("import sys; print('{\"complete\":false}'); sys.exit(42)")
        details = json.loads(diagnostic(result))
        self.assertEqual(details["exitCode"], 42)
        self.assertEqual(details["exitCodeHex"], "0000002a")
        self.assertIn('"complete":false', details["stdout"])
        self.assertEqual(details["stderr"], "")
        self.assertFalse(details["observationTimedOut"])
        self.assertGreater(details["pid"], 0)

    def test_stderr_refusal_drains_concurrently(self):
        result = self.worker("import sys; sys.stderr.write('x'*100000); sys.exit(7)")
        self.assertEqual(result.returncode, 7)
        self.assertEqual(len(result.stderr), 100000)
        self.assertFalse(result.observation_timed_out)

    def test_ready_acknowledged_before_completion(self):
        with tempfile.TemporaryDirectory(prefix="sc-observation-") as folder:
            marker = Path(folder) / "start.request"
            code = ("import json,sys,time; from pathlib import Path; "
                    "p=Path(json.load(sys.stdin)['marker']); print('{\"event\":\"ready\"}',flush=True); "
                    "\nwhile not p.exists(): time.sleep(.005)\n"
                    "print('{\"complete\":true}')")
            packets = []
            def acknowledge(line):
                packet = json.loads(line); packets.append(packet)
                if packet.get("event") == "ready": marker.write_text("start")
            result = self.worker(code, json.dumps({"marker": str(marker)}),
                                 on_stdout_line=acknowledge)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(packets, [{"event": "ready"}, {"complete": True}])

    def test_unresponsive_worker_and_full_input_pipe_are_bounded(self):
        started = time.monotonic()
        result = self.worker("import time; time.sleep(30)", "x" * 1000000,
                             timeout=.25)
        self.assertTrue(result.observation_timed_out)
        self.assertNotEqual(result.returncode, 0)
        self.assertLess(time.monotonic() - started, 5)

    def test_output_flood_is_bounded(self):
        result = self.worker("import sys;\nwhile True: sys.stdout.write('x'*4096); sys.stdout.flush()",
                             maximum_output_bytes=8192)
        self.assertTrue(result.output_limited)
        self.assertLessEqual(len(result.stdout.encode()), 8192)
        self.assertNotEqual(result.returncode, 0)

    def test_direct_exit_with_inherited_pipe_does_not_block_close(self):
        code = ("import subprocess,sys; "
                "p=subprocess.Popen([sys.executable,'-c','import time; time.sleep(8)']); "
                "print('owned pipe holder '+str(p.pid),flush=True)")
        started = time.monotonic()
        result = self.worker(code, timeout=2)
        try:
            self.assertEqual(result.returncode, 0)
            self.assertTrue(result.observation_timed_out)
            self.assertIn("owned pipe holder ", result.stdout)
            self.assertLess(time.monotonic() - started, 5.5)
        finally:
            if result.stdout.startswith("owned pipe holder "):
                # The owned child sleeps eight seconds; retire it before that
                # deadline rather than leaving a background test process.
                try: os.kill(int(result.stdout.split()[-1]), signal.SIGTERM)
                except ProcessLookupError: pass


if __name__ == "__main__":
    unittest.main()
