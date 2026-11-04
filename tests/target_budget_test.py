#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fixtures for the current target-budget heuristic and CLI; see target_budget_fixtures/README.md.
Run independently of the toolchain: python3 tests/target_budget_test.py
"""
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "target_budget.py"
# Loading without __main__ keeps the production script and CLI unchanged.
ESTIMATOR = runpy.run_path(str(SCRIPT))
FUNCS = ESTIMATOR["FUNCS"]
OPTIONAL = ESTIMATOR["OPTIONAL"]


class SpanFixtures(unittest.TestCase):
    def test_real_pi32v2_listing(self):
        fns = ESTIMATOR["functions"](HERE / "target_budget_fixtures" / "real_pi32v2.dis")
        self.assertEqual(set(fns), {"dv_out"})
        self.assertEqual(ESTIMATOR["cost"](fns["dv_out"]),
                         dict(insns=40, loop=35, div=0, call=0, cost=35))

    def test_loop_costs(self):
        fns = ESTIMATOR["functions"](HERE / "target_budget_fixtures" / "loops.dis")
        # Literal expected observations: insns, loop, div, call, cost.
        expected = {
            "analog_render": (4, 2, 0, 0, 2),       # simple loop
            "phase_render": (6, 4, 0, 0, 10),      # nested loop
            "lofi_render": (7, 4, 0, 0, 4),        # siblings, no nesting
            "sample_render": (6, 4, 0, 0, 4),      # split back edges, same head
            "formant_render": (6, 4, 0, 0, 4),     # acyclic backward path: false positive
            "trio_render": (5, 3, 0, 1, 3),        # callee's cost is not added
            "trio_pass": (4, 3, 1, 0, 11),
            "wheel_render": (5, 3, 1, 0, 11),       # outside divide gets no penalty
            "grain_render": (4, 2, 0, 0, 2),       # ordinary instruction inside loop
            "grain_block": (12, 10, 0, 0, 298),    # depth capped at four
            "mod_begin": (3, 0, 0, 0, 0),          # no loop, even with call/divide
        }
        self.assertEqual(set(fns), set(expected))
        for name, values in expected.items():
            with self.subTest(function=name):
                self.assertEqual(ESTIMATOR["cost"](fns[name]),
                                 dict(zip(("insns", "loop", "div", "call", "cost"), values)))

    def test_parser_boundaries_and_jump_table_data(self):
        with tempfile.TemporaryDirectory(dir=HERE.parent / "build") as tmp:
            dis = Path(tmp) / "parser.dis"
            dis.write_text("""analog_render:
 100: 00 00  r0 = 0
.Llocal:
 102: 00 00  r1 += 1
 104: 00 00  < 0 : 0x102 >
 106: 00 00  if r1 < r2 goto -4 <analog_render: 102 >
untracked_helper:
 200: 00 00  call <analog_render>
phase_render:
Disassembly of section .c1_text:
mix_part:
 300: 00 00  ret
""")
            self.assertEqual(ESTIMATOR["functions"](dis), {
                "analog_render": [(0x100, "r0 = 0"), (0x102, "r1 += 1"),
                                  (0x106, "if r1 < r2 goto -4 <analog_render: 102 >")],
                "phase_render": [],
                "mix_part": [(0x300, "ret")],
            })


    def test_incomplete_instruction_rows_are_rejected(self):
        with tempfile.TemporaryDirectory(dir=HERE.parent / "build") as tmp:
            dis = Path(tmp) / "undecoded.dis"
            for row in (" 102: 00 00  .word 0x1234", " 102: 00 00  (bad)", " 102: 00 00  unknown r0",
                        " 102: missing instruction bytes", " 102: 00 00  unknown_instruction r0",
                        " 102: 00 00  "):
                with self.subTest(row=row):
                    dis.write_text("analog_render:\n 100: 00 00  r0 = 0\n" + row + "\n")
                    with self.assertRaisesRegex(ValueError, "analog_render: (unparsed|undecoded) instruction"):
                        ESTIMATOR["functions"](dis)


class CommandLineFixtures(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=HERE.parent / "build")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.dis = self.root / "input.dis"
        self.budget = self.root / "budget.txt"
        self.required = [name for name in FUNCS if name not in OPTIONAL]
        self.write_dis()
        self.write_budget()

    def write_dis(self, omit=(), extra=(), loops=None):
        loops = loops or {}
        lines = []
        for name in self.required + list(extra):
            if name in omit:
                continue
            # Independent synthetic function with a requested number of loop instructions.
            count = loops.get(name, 2)
            lines.append(f"{name}:\n")
            for i in range(count - 1):
                lines.append(f" {0x100 + 2 * i:x}: 00 00  r0 += 1\n")
            offset = 2 * (count - 1)
            lines.append(f" {0x100 + offset:x}: 00 00  if r0 < r1 goto -{offset} <{name}: 100 >\n")
            lines.append(f" {0x100 + 2 * count:x}: 00 00  ret\n")
        self.dis.write_text("".join(lines))

    def write_budget(self, omit=(), overrides=None, extra=""):
        overrides = overrides or {}
        self.budget.write_text("# synthetic budget\n" + "".join(
            f"{name} {overrides.get(name, 2)}\n" for name in self.required if name not in omit) + extra)

    def cli(self, update=None, defaults=False):
        env = os.environ.copy()
        env.pop("BUDGET_UPDATE", None)  # Runner may intentionally update real budgets.
        if update is not None:
            env["BUDGET_UPDATE"] = update
        args = [] if defaults else [str(self.dis), str(self.budget)]
        return subprocess.run([sys.executable, str(SCRIPT), *args], cwd=self.root,
                              env=env, capture_output=True, text=True)

    def assert_status(self, result, status):
        self.assertEqual(result.returncode, status, result.stdout + result.stderr)

    def test_complete_input_passes_without_rewriting(self):
        before = self.budget.read_bytes()
        result = self.cli()
        self.assert_status(result, 0)
        self.assertNotIn("FAIL", result.stdout)
        self.assertNotIn("no budget", result.stdout)
        self.assertEqual(result.stdout.count("target:"), len(self.required))
        self.assertEqual(self.budget.read_bytes(), before)

    def test_tolerance_boundary_and_lower_cost_note(self):
        self.write_budget(overrides={"analog_render": 10})
        for count, status, message in ((11, 0, "ok"), (12, 1, "OVER BUDGET (+20 %"),
                                        (2, 0, "note: -80 %")):
            with self.subTest(loop_instructions=count):
                self.write_dis(loops={"analog_render": count})
                result = self.cli()
                self.assert_status(result, status)
                row = next(line for line in result.stdout.splitlines() if "analog_render" in line)
                self.assertIn(message, row)

    def test_missing_required_function_fails_even_on_update(self):
        self.write_dis(omit=("analog_render",))
        for update in (None, "1"):
            with self.subTest(update=update):
                result = self.cli(update=update)
                self.assert_status(result, 1)
                self.assertIn("FAIL analog_render not found", result.stdout)
        self.assertNotIn("analog_render", self.budget.read_text())

    def test_empty_required_function_is_missing(self):
        self.write_dis(omit=("analog_render",))
        with self.dis.open("a") as f:
            f.write("analog_render:\n")
        result = self.cli()
        self.assert_status(result, 1)
        self.assertIn("FAIL analog_render not found", result.stdout)

    def test_missing_budget_entry_warns_but_passes(self):
        self.write_budget(omit=("analog_render",))
        result = self.cli()
        self.assert_status(result, 0)
        self.assertIn("no budget (BUDGET_UPDATE=1 adds it)", result.stdout)

    def test_missing_budget_file_warns_but_passes(self):
        self.budget.unlink()
        result = self.cli()
        self.assert_status(result, 0)
        self.assertEqual(result.stdout.count("no budget"), len(self.required) - len(ESTIMATOR["ONE_CORE"]))
        self.assertFalse(self.budget.exists())

    def test_undecoded_back_edge_fails_without_rewriting(self):
        text = self.dis.read_text().replace("if r0 < r1 goto -2 <analog_render: 100 >", ".word 0xdeadbeef", 1)
        self.dis.write_text(text)
        before = self.budget.read_bytes()
        for update in (None, "1"):
            result = self.cli(update=update)
            self.assert_status(result, 1)
            self.assertIn("analog_render: undecoded instruction", result.stdout)
            self.assertNotIn("note: -100 %", result.stdout)
            self.assertEqual(self.budget.read_bytes(), before)

    def test_negative_budget_fails_cleanly_before_update(self):
        for value in (-1, -9):
            for update in (None, "1"):
                with self.subTest(value=value, update=update):
                    self.write_budget(overrides={"analog_render": value})
                    before = self.budget.read_bytes()
                    result = self.cli(update=update)
                    self.assert_status(result, 1)
                    self.assertIn("invalid budget for analog_render", result.stdout)
                    self.assertNotIn("Traceback", result.stderr)
                    self.assertEqual(self.budget.read_bytes(), before)

    def test_zero_budget_reports_growth_without_dividing(self):
        self.write_budget(overrides={"analog_render": 0})
        result = self.cli()
        self.assert_status(result, 1)
        self.assertIn("OVER BUDGET (cost 2 exceeds zero baseline)", result.stdout)
        self.assertNotIn("Traceback", result.stderr)

    def test_zero_cost_update_round_trip(self):
        text = self.dis.read_text()
        start = text.index("analog_render:\n")
        end = text.index("phase_render:\n", start)
        self.dis.write_text(text[:start] + "analog_render:\n 100: 00 00  ret\n" + text[end:])
        self.assert_status(self.cli(update="1"), 0)
        self.assertIn("analog_render 0\n", self.budget.read_text())
        self.assert_status(self.cli(), 0)

    def test_extra_entry_ignored_and_short_line_skipped(self):
        self.write_budget(extra="untracked_helper 999\nmalformed_single_word\n")
        result = self.cli()
        self.assert_status(result, 0)
        self.assertNotIn("untracked_helper", result.stdout)

    def test_non_numeric_budget_fails_before_update(self):
        for line in ("analog_render not-a-number\n", "untracked_helper not-a-number\n"):
            for update in (None, "1"):
                with self.subTest(line=line, update=update):
                    self.write_budget(extra=line)
                    before = self.budget.read_bytes()
                    result = self.cli(update=update)
                    self.assert_status(result, 1)
                    self.assertIn("invalid budget", result.stdout)
                    self.assertNotIn("Traceback", result.stderr)
                    self.assertEqual(self.budget.read_bytes(), before)

    def test_optional_function_fallback_and_explicit_budget(self):
        self.write_dis(extra=("uac_tap48",), loops={"uac_tap48": 600})
        result = self.cli()
        self.assert_status(result, 1)
        self.assertIn(f"(budget {OPTIONAL['uac_tap48']}) OVER BUDGET", result.stdout)
        self.write_budget(extra="uac_tap48 600\n")
        self.assert_status(self.cli(), 0)

    def test_one_core_fallback_and_dual_core_explicit_budget(self):
        result = self.cli()
        self.assert_status(result, 0)
        self.assertIn(f"(budget {ESTIMATOR['ONE_CORE']['fm1_alnk0_irq']})", result.stdout)
        self.write_dis(extra=("mix_part",))
        result = self.cli()
        self.assert_status(result, 0)
        row = next(line for line in result.stdout.splitlines() if "fm1_alnk0_irq" in line)
        self.assertIn("(budget 2) ok", row)

    def test_update_rewrites_required_only_then_checks(self):
        self.budget.unlink()
        self.write_dis(extra=("uac_tap48",), loops={"analog_render": 12})
        result = self.cli(update="1")
        self.assert_status(result, 0)
        self.assertIn(f"rewritten ({len(self.required) + 1} functions)", result.stdout)
        self.assertNotIn("OVER BUDGET", result.stdout)
        rows = self.budget.read_text().splitlines()
        self.assertEqual([line for line in rows if not line.startswith("#")],
                         [f"{name} {12 if name == 'analog_render' else 2}" for name in self.required])
        self.assert_status(self.cli(), 0)

    def test_nonempty_zero_update_value_also_rewrites(self):
        self.write_budget(extra="untracked_helper 999\n")
        self.assert_status(self.cli(update="0"), 0)
        self.assertNotIn("untracked_helper", self.budget.read_text())

    def test_update_accepts_growth_against_existing_budget(self):
        self.write_dis(loops={"analog_render": 12})
        self.assert_status(self.cli(), 1)
        result = self.cli(update="1")
        self.assert_status(result, 0)
        self.assertNotIn("OVER BUDGET", result.stdout)
        self.assertIn("analog_render 12\n", self.budget.read_text())
        self.assert_status(self.cli(), 0)

    def test_missing_disassembly_skips_without_updating(self):
        self.dis.unlink()
        before = self.budget.read_bytes()
        result = self.cli(update="1")
        self.assert_status(result, 0)
        self.assertIn("target: skip", result.stdout)
        self.assertEqual(self.budget.read_bytes(), before)

    def test_default_paths(self):
        (self.root / "build").mkdir()
        (self.root / "tests").mkdir()
        self.dis.rename(self.root / "build" / "felucca.dis")
        self.budget.rename(self.root / "tests" / "target_budget.txt")
        self.assert_status(self.cli(defaults=True), 0)


if __name__ == "__main__":
    (HERE.parent / "build").mkdir(exist_ok=True)
    unittest.main()
