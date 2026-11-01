#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise compact output with fake tests, without building firmware."""

import json
import os
from pathlib import Path
import shutil
import shlex
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent


class OutputTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "tests").mkdir()
        shutil.copy(ROOT / "tests/test_output.py", self.root / "tests")
        # Use the real entry/dispatch and run function, replacing only the suite.
        self.prefix = (ROOT / "tests/run_tests.sh").read_text().split('\nrun "compact output reporting contracts"')[0]
        self.env = dict(os.environ)
        self.env.pop("FELUCCA_TEST_OUTPUT_TOKEN", None)

    def fixture(self, body):
        script = self.root / "tests/run_tests.sh"
        script.write_text(self.prefix + "\n" + body + '\n[ "$fail" -eq 0 ]\n')
        return ["sh", str(script)]

    def run_fixture(self, body, compact=True):
        command = self.fixture(body) + (["--ai-output"] if compact else [])
        result = subprocess.run(command, env=self.env, capture_output=True, text=True)
        summaries = list(self.root.glob("build/host/test-run-*/summary.json"))
        summary = json.loads(summaries[0].read_text()) if summaries else None
        return result, summary

    def test_success_raw_log_and_stripped_flag(self):
        result, summary = self.run_fixture('test "$#" -eq 0\nrun "ok" sh -c \'printf "raw success\\n"\'')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASSED: ok", result.stdout)
        self.assertNotIn("raw success", result.stdout)
        self.assertEqual(summary["counts"]["passed"], 1)
        raw = (self.root / summary["log"]).read_text()
        self.assertEqual(raw, "== ok\nraw success\n")

    def test_output_without_final_newline(self):
        result, summary = self.run_fixture('run "partial" printf "warning: no newline"')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(summary["counts"]["passed"], 1)
        self.assertEqual(summary["counts"]["warnings"], 1)
        self.assertIsNone(summary["incomplete_test"])
        self.assertEqual((self.root / summary["log"]).read_bytes(), b"== partial\nwarning: no newline")

    def test_failure_continues_and_preserves_exit(self):
        result, summary = self.run_fixture('run "bad" sh -c \'echo failure >&2; exit 7\'\nrun "next" true')
        self.assertEqual(result.returncode, 1)
        self.assertIn("FAILED: bad (exit 7)", result.stdout)
        self.assertIn("PASSED: next", result.stdout)
        self.assertEqual(summary["status"], "failed")
        self.assertIn("failure", (self.root / summary["log"]).read_text())

    def test_hints_bounded_and_not_pass_counts(self):
        result, summary = self.run_fixture('run "mixed" sh -c \'echo "SKIP optional"; i=0; while [ "$i" -lt 30 ]; do echo "warning: detail"; i=$((i+1)); done\'')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(summary["counts"], {"passed": 1, "failed": 0, "warnings": 30, "skips": 1})
        self.assertEqual(result.stdout.count(" hint:"), 10)
        self.assertIn("SKIP hint:", result.stdout)
        self.assertEqual((self.root / summary["log"]).read_text().count("warning: detail"), 30)

    def test_preflight_missing_tool(self):
        result, summary = self.run_fixture('felucca_nonexistent_test_tool\nrun "unreachable" true')
        self.assertEqual(result.returncode, 127)
        self.assertEqual(summary["counts"]["passed"], 0)
        self.assertEqual(summary["status"], "failed")
        self.assertIn("not found", (self.root / summary["log"]).read_text())

    def test_setup_failure_after_pass_is_not_success(self):
        result, summary = self.run_fixture('run "ok" true\nfalse\nrun "unreachable" true')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(summary["counts"]["passed"], 1)
        self.assertEqual(summary["counts"]["failed"], 0)
        self.assertEqual(summary["status"], "failed")

    def test_standard_mode_transcript(self):
        result, summary = self.run_fixture('run "ok" echo raw\nrun "bad" false\nrun "next" true', compact=False)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "== ok\nraw\n== bad\n== next\n")
        self.assertIsNone(summary)

    def test_extra_argument_rejected(self):
        result = subprocess.run(self.fixture('run "unexpected" true') + ["--ai-output", "extra"],
                                env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage:", result.stderr)
        self.assertFalse(list(self.root.glob("build/host/test-run-*")))

    def test_signals_cancel_running_child(self):
        for sig, ignore in ((signal.SIGINT, False), (signal.SIGTERM, False), (signal.SIGINT, True)):
            with self.subTest(signal=sig, ignore=ignore):
                child = "import signal, time; from pathlib import Path; "
                if ignore:
                    child += "signal.signal(signal.SIGINT, signal.SIG_IGN); signal.signal(signal.SIGTERM, signal.SIG_IGN); "
                child += "Path('ready').write_text('ready'); time.sleep(30); Path('leaked').write_text('leaked')"
                command = self.fixture('run "waiting" python3 -c ' + shlex.quote(child))
                process = subprocess.Popen(command + ["--ai-output"], env=self.env,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                           preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_IGN))
                try:
                    deadline = time.monotonic() + 5
                    while not (self.root / "ready").exists() and time.monotonic() < deadline:
                        time.sleep(0.02)
                    self.assertTrue((self.root / "ready").exists())
                    process.send_signal(sig)
                    out, err = process.communicate(timeout=8)
                    self.assertEqual(process.returncode, 128 + sig, (out, err))
                    self.assertIn("CANCELLED:", out)
                    self.assertFalse((self.root / "leaked").exists())
                    summaries = sorted(self.root.glob("build/host/test-run-*/summary.json"), key=lambda p: p.stat().st_mtime_ns)
                    self.assertEqual(json.loads(summaries[-1].read_text())["status"], "cancelled")
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate()
                    (self.root / "ready").unlink(missing_ok=True)

    def test_cancel_stops_redirected_descendant(self):
        grandchild = (
            "import os, signal, time; from pathlib import Path; "
            "signal.signal(signal.SIGINT, signal.SIG_IGN); "
            "signal.signal(signal.SIGTERM, signal.SIG_IGN); "
            "Path('ready').write_text(str(os.getpid())); time.sleep(30)"
        )
        parent = (
            "import subprocess, sys, time; "
            f"subprocess.Popen([sys.executable, '-c', {grandchild!r}], "
            "stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); time.sleep(30)"
        )
        command = self.fixture('run "redirected" python3 -c ' + shlex.quote(parent))
        process = subprocess.Popen(command + ["--ai-output"], env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        descendant = None
        try:
            deadline = time.monotonic() + 5
            while not (self.root / "ready").exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            descendant = int((self.root / "ready").read_text())
            process.send_signal(signal.SIGTERM)
            out, err = process.communicate(timeout=8)
            self.assertEqual(process.returncode, 143, (out, err))
            # A killed orphan can briefly remain a zombie until init reaps it.
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                state = subprocess.run(["ps", "-o", "stat=", "-p", str(descendant)],
                                       capture_output=True, text=True).stdout.strip()
                if not state or state.startswith("Z"):
                    break
                time.sleep(0.02)
            self.assertTrue(not state or state.startswith("Z"), state)
        finally:
            if descendant is not None:
                try:
                    os.kill(descendant, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if process.poll() is None:
                process.kill()
                process.communicate()


if __name__ == "__main__":
    unittest.main()
