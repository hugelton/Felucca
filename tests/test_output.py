#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compact presentation for the sequential host runner; never selects tests."""

import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import threading
import uuid


def compact(command, output_root):
    directory = Path(tempfile.mkdtemp(prefix="test-run-", dir=output_root))
    log_path = directory / "output.log"
    token = uuid.uuid4().hex
    env = dict(os.environ, FELUCCA_TEST_OUTPUT_TOKEN=token)
    counts = {"passed": 0, "failed": 0, "warnings": 0, "skips": 0}
    active = None
    interrupted = 0
    hints = 0

    def display(text):
        print(text[:200], flush=True)

    def diagnostics(line):
        nonlocal hints
        for pattern, key, label in ((r"\bwarn(?:ing)?\b", "warnings", "WARNING"),
                                    (r"\bskip(?:ped)?\b", "skips", "SKIP")):
            if re.search(pattern, line, re.I):
                counts[key] += 1
                if hints < 10:
                    display(label + " hint: " + line)
                    hints += 1

    process = None
    escalation = []

    def forward(signum):
        try:
            os.killpg(process.pid, signum)
        except ProcessLookupError:
            pass

    def cancel(signum, _frame):
        nonlocal interrupted
        interrupted = interrupted or signum
        if process is None:
            return
        forward(signum)
        if not escalation:
            # Tests may ignore signals or fork while one arrives. Stop remaining
            # descendants too, including ones retaining the output pipe.
            for delay, next_signal in ((2, signal.SIGTERM), (4, signal.SIGKILL)):
                timer = threading.Timer(delay, forward, args=(next_signal,))
                timer.daemon = True
                escalation.append(timer)
                timer.start()

    old_handlers = {sig: signal.signal(sig, cancel) for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        # Install handlers before spawning: even callers which ignore SIGINT must
        # give the child a default SIGINT disposition when it execs.
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   env=env, start_new_session=True)
        if interrupted:
            cancel(interrupted, None)
        # Decode for display only; retain the original bytes, including malformed UTF-8.
        with log_path.open("wb") as raw:
            for data in process.stdout:
                # A command need not end its output with a newline. Keep any
                # bytes preceding a status marker in the raw transcript too.
                before, marker, after = data.partition((token + " ").encode())
                if marker:
                    raw.write(before)
                    diagnostics(before.decode("utf-8", errors="replace").rstrip("\r\n"))
                    data = marker + after
                line = data.decode("utf-8", errors="replace").rstrip("\r\n")
                if line.startswith(token + " start "):
                    active = line[len(token + " start "):]
                    continue
                if line.startswith(token + " end "):
                    code = int(line[len(token + " end "):])
                    state = "failed" if code else "passed"
                    counts[state] += 1
                    display(f"{state.upper()}: {active} (exit {code})")
                    active = None
                    continue
                raw.write(data)
                diagnostics(line)
        if interrupted:
            # EOF can arrive while a redirected descendant is still alive. Kill
            # the remaining group before wait() releases our child's PID.
            forward(signal.SIGKILL)
        code = process.wait()
    finally:
        for timer in escalation:
            timer.cancel()
        if process is not None:
            process.stdout.close()
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)
    code = 128 + interrupted if interrupted else (128 - code if code < 0 else code)
    status = "cancelled" if interrupted else ("failed" if code else "completed")
    # A terminated runner may leave a test without its closing status marker.
    if active is not None:
        display(f"INCOMPLETE: {active}")
    summary = dict(status=status, exit_code=code, counts=counts, incomplete_test=active,
                   log=str(log_path), diagnostic_limit=10)
    (directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    display(f"{status.upper()}: {counts['passed']} passed, {counts['failed']} failed; "
            f"{counts['warnings']} warning / {counts['skips']} skip line hints")
    print(f"Full log: {log_path}\nSummary: {directory / 'summary.json'}", flush=True)
    return code


if __name__ == "__main__":
    sys.exit(compact(["sh", sys.argv[1]], "build/host"))
