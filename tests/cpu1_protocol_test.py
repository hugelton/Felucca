#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual HAL mailbox protocol with deterministic ordering hooks.

Volatile observations expose publication markers before payload; only a following
barrier makes payload visible. Store hooks require a preceding release. The real
worker/start/run/wait bodies run without pthread synchronization. This verifies
software ordering under the HAL CSYNC contract, not physical cache coherence or
real-device deadlines.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function_source(header, signature):
    body = header[header.index(signature):]
    depth = 0
    for pos in range(body.index("{"), len(body)):
        if body[pos] == "{":
            depth += 1
        elif body[pos] == "}":
            depth -= 1
            if depth == 0:
                return body[:pos + 1]
    raise AssertionError(f"unterminated function: {signature}")


def protocol_source():
    header = (ROOT / "firmware/hal/fm1_cpu1.h").read_text()
    record = re.search(r"typedef struct \{.*?\} fm1_c1_mb_t;", header, re.S).group()
    signatures = ("void __attribute__((section(\".c1_text\"), noreturn, used)) fm1_c1_main(void)",
                  "static void fm1_cpu1_hold(void)", "static int fm1_cpu1_start(void)",
                  "static inline int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)",
                  "static inline int fm1_cpu1_wait(void)")
    return record, "\n".join(function_source(header, signature) for signature in signatures)


def instrument(body):
    body = body.replace('__attribute__((section(".c1_text"), noreturn, used)) ', "")
    body = body.replace("FM1_C1_SYNC();", "barrier();")
    body = body.replace("*(volatile uint32_t *)(0x1EEF300u + 4u * i)", "irq_bank[i]")
    addresses = {}
    def mmio(match):
        address = match.group(1)
        return f"registers[{addresses.setdefault(address, len(addresses))}]"
    body = re.sub(r"\*\(volatile uint32_t \*\)(0x[0-9A-Fa-f]+u)", mmio, body)
    body = body.replace("fm1_c1_mb.bad++;", "STORE(bad, LOAD(bad) + 1u);")
    body = re.sub(r"fm1_c1_trace = ([^;]+);", r"STORE_TRACE(\1);", body)
    # Keep lvalue stores distinct from observations, including == comparisons.
    body = re.sub(r"fm1_c1_mb\.(\w+)\s*=(?!=)\s*([^;]+);",
                  r"STORE(\1, \2);", body)
    body = re.sub(r"fm1_c1_mb\.(\w+)", r"LOAD(\1)", body)
    return body


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
@RECORD@
static fm1_c1_mb_t fm1_c1_mb;
static uint32_t fm1_c1_trace, fm1_c1_wait_max;
static uint8_t fm1_c1_on, fm1_c1_gave_up, fm1_c1_marked;
static uint32_t _c1_ustack[1024], _c1_sstack_top[1], registers[16], irq_bank[32];
#define FM1_C1_STACK_WORDS 1024u
#define FM1_C1_MARK 0x43314D4Bu
#define FM1_C1_CHECK(fn, arg, job) ((uint32_t)(uintptr_t)(fn) ^ (arg) ^ (job) * 0x9E3779B1u)
static volatile uint32_t clock_register, control_register;
static uint32_t ticks;
#define FM1_C1_CLK clock_register
#define FM1_C1_CON control_register
#define FM1_C1_T4 timer()
static unsigned scenario, worker, sequence, last_sync, body_store, work_store;
static unsigned observation, acquired, job_reads, calls, halts, pending, payload, trace_store, boot_done_load;
static uint32_t initial_done, next_job, argument;
static jmp_buf worker_exit;
static void fm1_c1_entry(void) {}
static uint32_t timer(void) { ticks += 120001u; return ticks; }
static void work(uint32_t arg) {
    assert(worker && acquired && arg == argument);
    calls++;
    work_store = ++sequence;
}
static void barrier(void) {
    last_sync = ++sequence;
    if (pending) {
        acquired = 1;
        pending = 0;
        payload = 0xCAFEu;
        if (worker) {
            fm1_c1_mb.fn = work;
            fm1_c1_mb.arg = argument;
            fm1_c1_mb.check = FM1_C1_CHECK(work, argument, next_job);
        }
    }
}
static void observe_load(const char *field) {
    sequence++;
    if (worker && !strcmp(field, "job")) {
        if (job_reads++ == (scenario >= 11 ? 2u : 1u)) longjmp(worker_exit, 1);
        fm1_c1_mb.job = next_job;
        pending = 1;
        acquired = 0;
        observation = sequence;
    } else if (worker && !strcmp(field, "done")) {
        assert(last_sync > trace_store);
        boot_done_load = sequence;
    } else if (!worker && !strcmp(field, "done") && scenario == 0) {
        fm1_c1_mb.done = fm1_c1_mb.job;
        pending = 1;
        acquired = 0;
        observation = sequence;
    } else if (!worker && !strcmp(field, "bad") && scenario == 1) {
        fm1_c1_mb.bad = 1;
        pending = 1;
        acquired = 0;
        observation = sequence;
    } else if (!worker && !strcmp(field, "alive") && scenario == 3) {
        assert(last_sync > body_store);
        fm1_c1_mb.alive = 1;
        pending = 1;
        acquired = 0;
        observation = sequence;
    }
    if (worker && (!strcmp(field, "fn") || !strcmp(field, "arg") || !strcmp(field, "check")))
        assert(acquired && last_sync > observation);
}
static void observe_store(const char *field) {
    sequence++;
    if (!strcmp(field, "job")) assert(last_sync > body_store);
    else if (!strcmp(field, "fn") || !strcmp(field, "arg") || !strcmp(field, "check") ||
             !strcmp(field, "bad_fn") || !strcmp(field, "bad_job") || !strcmp(field, "bad_done"))
        body_store = sequence;
    else if (worker && !strcmp(field, "done")) assert(last_sync > work_store);
    else if (worker && !strcmp(field, "bad")) assert(last_sync > body_store);
    else if (worker && !strcmp(field, "alive")) assert(last_sync > boot_done_load);
}
#define LOAD(field) (observe_load(#field), fm1_c1_mb.field)
#define STORE(field, value) do { observe_store(#field); fm1_c1_mb.field = (value); } while (0)
#define STORE_TRACE(value) do { trace_store = ++sequence; fm1_c1_trace = (value); } while (0)
static void fm1_cpu1_halt(void) {
    if (scenario == 1) assert(acquired && payload == 0xCAFEu && last_sync > observation);
    halts++;
}
@PROTOCOL@
int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = (unsigned)atoi(argv[1]);
    fm1_c1_on = 1;
    fm1_c1_mb.job = 17;
    fm1_c1_mb.done = 16;
    if (scenario <= 2) {
        int result = fm1_cpu1_wait();
        if (!scenario) assert(!result && acquired && payload == 0xCAFEu && last_sync > observation && !halts);
        else assert(result == -1 && !fm1_c1_on && halts == 1 && fm1_c1_gave_up == scenario);
    } else if (scenario <= 4) {
        fm1_c1_on = 0;
        int result = fm1_cpu1_start();
        assert(fm1_c1_marked && fm1_c1_mb.done == fm1_c1_mb.job && !fm1_c1_mb.fn);
        if (scenario == 3) assert(!result && fm1_c1_on && acquired && last_sync > observation);
        else assert(result == -1 && !fm1_c1_on && halts == 2);
        for (unsigned i = 0; i < FM1_C1_STACK_WORDS; i++) assert(_c1_ustack[i] == FM1_C1_MARK);
    } else if (scenario == 5) {
        assert(fm1_cpu1_run(work, 23));
        assert(fm1_c1_mb.job == 18 && fm1_c1_mb.fn == work && fm1_c1_mb.arg == 23);
        assert(fm1_c1_mb.check == FM1_C1_CHECK(work, 23u, 18u));
        fm1_c1_on = 0;
        uint32_t job = fm1_c1_mb.job;
        assert(!fm1_cpu1_run(work, 24) && fm1_c1_mb.job == job);
    } else {
        worker = 1;
        initial_done = scenario == 10 ? UINT32_MAX : 17u;
        fm1_c1_mb.done = initial_done;
        next_job = initial_done + (scenario == 7 ? 0u : scenario == 8 || scenario == 11 ? 2u : 1u);
        argument = 23u;
        if (!setjmp(worker_exit)) fm1_c1_main();
        assert(fm1_c1_trace == 0xC1000002u && fm1_c1_mb.alive && job_reads == (scenario >= 11 ? 3u : 2u));
        if (scenario == 6 || scenario == 10 || scenario == 12) assert(calls == 1 && fm1_c1_mb.done == next_job && !fm1_c1_mb.bad);
        else if (scenario == 7) assert(!calls && !fm1_c1_mb.bad && fm1_c1_mb.done == initial_done);
        else assert(!calls && fm1_c1_mb.bad == 1 && fm1_c1_mb.bad_job == next_job && fm1_c1_mb.bad_done == initial_done);
    }
    return 0;
}
'''


class Cpu1Protocol(unittest.TestCase):
    def compile_run(self, body, scenarios):
        record, _ = protocol_source()
        source = HARNESS.replace("@RECORD@", record).replace("@PROTOCOL@", instrument(body))
        # A corrupted check must exercise the actual worker's refusal path.
        if 9 in scenarios:
            source = source.replace("fm1_c1_mb.check = FM1_C1_CHECK(work, argument, next_job);",
                                    "fm1_c1_mb.check = scenario == 9 ? 0u : FM1_C1_CHECK(work, argument, next_job);")
        with tempfile.TemporaryDirectory(prefix="felucca-cpu1-protocol-") as temporary:
            path = Path(temporary)
            (path / "fixture.c").write_text(source)
            executable = path / "protocol"
            subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O2",
                            "-Wall", "-Wextra", "-Werror", str(path / "fixture.c"),
                            "-o", str(executable)], check=True)
            return [subprocess.run([str(executable), str(scenario)], capture_output=True, text=True)
                    for scenario in scenarios]

    def test_actual_mailbox_paths(self):
        _, body = protocol_source()
        for scenario, result in enumerate(self.compile_run(body, range(13))):
            with self.subTest(scenario=scenario):
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_acquires_are_detected(self):
        _, body = protocol_source()
        for comment, scenario in (("acquire its finished payload", 0), ("acquire its failure details", 1),
                                  ("acquire the worker's published boot state", 3),
                                  ("acquire core 0's boot state", 6)):
            with self.subTest(comment=comment):
                mutant, count = re.subn(r"^.*FM1_C1_SYNC\(\);[^\n]*" + re.escape(comment) + r"[^\n]*\n", "", body, flags=re.M)
                self.assertEqual(count, 1)
                self.assertNotEqual(self.compile_run(mutant, [scenario])[0].returncode, 0)
        mutant = body.replace("uint32_t j = fm1_c1_mb.job;\n        FM1_C1_SYNC();",
                              "uint32_t j = fm1_c1_mb.job;")
        self.assertNotEqual(mutant, body)
        self.assertNotEqual(self.compile_run(mutant, [6])[0].returncode, 0)

    def test_missing_payload_releases_are_detected(self):
        _, body = protocol_source()
        mutant, count = re.subn(r"^.*FM1_C1_SYNC\(\);[^\n]*release the guards/trace[^\n]*\n", "", body, flags=re.M)
        self.assertEqual(count, 1)
        self.assertNotEqual(self.compile_run(mutant, [6])[0].returncode, 0)
        for marker, scenario in (("fm1_c1_mb.job = job;", 5), ("fm1_c1_mb.done = j;", 6),
                                 ("fm1_c1_mb.bad++;", 8)):
            with self.subTest(marker=marker):
                mutant = body.replace("FM1_C1_SYNC();\n" + " " * (4 if scenario == 5 else 16 if scenario == 6 else 12) + marker, marker)
                self.assertNotEqual(mutant, body)
                self.assertNotEqual(self.compile_run(mutant, [scenario])[0].returncode, 0)


if __name__ == "__main__":
    unittest.main()
