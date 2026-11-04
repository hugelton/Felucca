#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual USB/editor/OTA dispatch with an arrival between service checks.

Compile the existing editor host fixture, real ota.c, and the outer main.c
service composition. The sole receive adapter publishes a real USB SysEx frame
after the editor's empty frame observation, without changing that observation.
This is a deterministic main-loop ownership regression, not USB timing evidence.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function_source(source, signature):
    """Extract the existing fixture reset without duplicating its state setup."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    for i in range(opening + 1, len(source)):
        depth += (source[i] == "{") - (source[i] == "}")
        if not depth:
            return source[start:i + 1]
    raise AssertionError("unterminated fixture function")


ARRIVAL = r'''
#undef ota_frame_get
static uint8_t dispatch_frame[128];
static uint32_t dispatch_frame_n, dispatch_armed, dispatch_injections;
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    int ready = dispatch_real_frame_get(p, n);
    if (!ready && dispatch_armed) {
        dispatch_armed = 0;
        dispatch_injections++;
        for (uint32_t i = 0; i < dispatch_frame_n; i++)
            sysex_byte(dispatch_frame[i]);
    }
    return ready; /* retain the editor's real empty observation */
}
'''

FIXTURE = r'''
#define FELUCCA_ID "FM-1_900"
@EDITOR_FIXTURE@
static uint32_t dispatch_flash_hooks;
static int ota_erase(uint32_t off) { (void)off; dispatch_flash_hooks++; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{ (void)off; (void)p; (void)n; dispatch_flash_hooks++; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n)
{ (void)off; (void)p; (void)n; dispatch_flash_hooks++; return -1; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; dispatch_flash_hooks++; }
static void ota_commit(const uint8_t *parm) { (void)parm; dispatch_flash_hooks++; }
#include "../firmware/bsp/chip/ota.c"
@RESET@
static void dispatch_once(void)
{
@MAIN_SERVICES@
}
static void publish_frame(void)
{
    for (uint32_t i = 0; i < dispatch_frame_n; i++)
        sysex_byte(dispatch_frame[i]);
}
static void prepare(uint32_t race)
{
    reset();
    dispatch_flash_hooks = dispatch_injections = 0;
    dispatch_armed = race;
    if (!race) publish_frame();
}
static uint32_t rounds(uint32_t race)
{
    dispatch_once();
    host_drain();
    /* A newly arrived frame may wait for the next main-loop iteration, but may not disappear. */
    dispatch_once();
    host_drain();
    if (race && dispatch_injections != 1u) {
        puts("FAIL: the empty-read arrival seam was not exercised");
        return 0;
    }
    if (dispatch_flash_hooks || host_erases || host_writes) {
        puts("FAIL: read/identity dispatch reached a flash or update hook");
        return 0;
    }
    return 1;
}
static void editor_frame(uint32_t cmd, uint8_t first, uint8_t second)
{
    const uint8_t request[] = {0xF0, ED_HDR0, ED_HDR1, ED_HDR2, (uint8_t)cmd, first, second, 0xF7};
    memcpy(dispatch_frame, request, sizeof request);
    dispatch_frame_n = sizeof request;
}
static int editor_race(uint32_t cmd, uint8_t first, uint8_t second)
{
    uint8_t expected[1024];
    uint32_t expected_n;
    editor_frame(cmd, first, second);
    prepare(0);
    if (!rounds(0)) return 1;
    expected_n = host_wire_n;
    if (expected_n < 8u || host_wire[4] != cmd || host_wire[5] != first || host_wire[6] != second) {
        puts("FAIL: uninterrupted editor control did not produce the requested reply");
        return 1;
    }
    memcpy(expected, host_wire, expected_n);
    prepare(1);
    if (!rounds(1)) return 1;
    if (host_wire_n != expected_n || memcmp(host_wire, expected, expected_n)) {
        printf("FAIL: cmd %u [%u,%u] reply lost or changed after empty-editor/OTA arrival: expected %u bytes, got %u\n",
               cmd, first, second, expected_n, host_wire_n);
        return 1;
    }
    if (sx_ready || sx_busy) { puts("FAIL: answered editor frame remains owned"); return 1; }
    printf("PASS: cmd %u [%u,%u] complete %u-byte reply survives arrival between services\n", cmd, first, second, expected_n);
    return 0;
}
static void identity_frame(void)
{
    const uint8_t request[] = {0, 0x59, 0x11, 0, 0, 0, 0xFF};
    dispatch_frame[0] = 0xF0;
    dispatch_frame_n = 1u + ota_pack7(request, sizeof request, dispatch_frame + 1);
    dispatch_frame[dispatch_frame_n++] = 0xF7;
}
static int identity(uint32_t race)
{
    uint8_t decoded[64];
    identity_frame();
    prepare(race);
    if (!rounds(race)) return 1;
    uint32_t n = host_wire_n > 2u ? ota_unpack7(host_wire + 1, host_wire_n - 2u, decoded, sizeof decoded) : 0u;
    if (n != 34u || decoded[0] || decoded[1] != 0x59 || decoded[2] != 0x11 || decoded[3] != 27u ||
        memcmp(decoded + 6, FELUCCA_ID, sizeof FELUCCA_ID - 1u) || sx_ready || sx_busy) {
        puts("FAIL: M-UPGRADE identity not answered/released");
        return 1;
    }
    printf("PASS: M-UPGRADE identity (%s)\n", race ? "arrival between services" : "already pending");
    return 0;
}
static int unknown(uint32_t race)
{
    const uint8_t request[] = {0xF0, 0x7D, 1, 2, 0xF7};
    memcpy(dispatch_frame, request, sizeof request);
    dispatch_frame_n = sizeof request;
    prepare(race);
    if (!rounds(race)) return 1;
    if (host_wire_n || sx_ready || sx_busy) { puts("FAIL: unknown frame not discarded cleanly"); return 1; }
    /* Cleanup must also leave room for the next legitimate editor request. */
    editor_frame(ED_DESC, 0, 29);
    publish_frame();
    if (!rounds(0) || host_wire_n < 8u || host_wire[4] != ED_DESC || host_wire[5] || host_wire[6] != 29u) {
        puts("FAIL: unknown frame blocks the next editor request");
        return 1;
    }
    printf("PASS: unknown frame cleanup (%s), subsequent editor request answered\n", race ? "arrival between services" : "already pending");
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    switch (atoi(argv[1])) {
    case 0: return editor_race(ED_DESC, 0, 29);
    case 1: return editor_race(ED_TRACK_STEP, 0, 50);
    case 2: return identity(0);
    case 3: return identity(1);
    case 4: return unknown(0);
    case 5: return unknown(1);
    default: return 2;
    }
}
'''


def main_services():
    source = (ROOT / "firmware/src/main.c").read_text()
    match = re.search(r"#if FELUCCA_OTA\n(?P<services>(?:(?!#endif).)*?)\s*if \(usb\.ota_req\)", source, re.S)
    if not match:
        raise AssertionError("could not locate the actual outer SysEx service block")
    services = match["services"]
    if "ed_service()" not in services or "ota_service()" not in services:
        raise AssertionError("outer service block no longer contains both protocol services")
    return services


class SysexDispatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = Path(os.environ.get("SYSEX_DISPATCH_OUT", ROOT / "build/sysex-dispatch"))
        cls.out.mkdir(parents=True, exist_ok=True)
        editor = (ROOT / "tests/editor_test.c").read_text()
        prefix = editor[:editor.index("static int check(")]
        prefix = prefix.replace("#define main hostsim_main", "#define ota_frame_get dispatch_real_frame_get\n#define main hostsim_main", 1)
        prefix = prefix.replace("#undef main", "#undef main\n" + ARRIVAL, 1)
        services = main_services()
        cls.services = services
        cls.source = (FIXTURE.replace("@EDITOR_FIXTURE@", prefix)
                      .replace("@RESET@", function_source(editor, "static void reset(void)"))
                      .replace("@MAIN_SERVICES@", services))
        (cls.out / "actual-main-services.c.txt").write_text(services)
        cls.exe, cmd, cc = cls.compile_source(cls.source, "dispatch")
        inputs = ["tests/editor_test.c", "firmware/src/main.c", "firmware/src/editor.c",
                  "firmware/bsp/chip/usb.c", "firmware/bsp/chip/ota.c"]
        metadata = {"compiler": subprocess.check_output([*cc, "--version"], text=True), "command": cmd,
                    "source_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in inputs}}
        (cls.out / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")

    @classmethod
    def compile_source(cls, source, name):
        cfile, executable = cls.out / (name + ".c"), cls.out / name
        cfile.write_text(source)
        cc = shlex.split(os.environ.get("CC", "cc"))
        cmd = [*cc, "-O2", "-w", "-I" + str(ROOT / "tests"), "-I" + str(ROOT / "build/gen"),
               "-I" + str(ROOT / "firmware/src"),
               "-I" + str(ROOT / "firmware/hal"), "-I" + str(ROOT / "firmware/bsp/chip"),
               "-I" + str(ROOT / "firmware/bsp/boards/fm1"), str(cfile), "-lm", "-o", str(executable)]
        compiled = subprocess.run(cmd, capture_output=True, text=True)
        (cls.out / (name + "-compile.log")).write_text(compiled.stdout + compiled.stderr)
        if compiled.returncode:
            raise AssertionError("host fixture compilation failed:\n" + compiled.stderr)
        return executable, cmd, cc

    def run_case(self, case, name):
        result = subprocess.run([str(self.exe), str(case)], capture_output=True, text=True)
        (self.out / (name + ".log")).write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_descriptor_arriving_after_empty_editor_check(self):
        self.run_case(0, "desc-race")

    def test_track_step_arriving_after_empty_editor_check(self):
        self.run_case(1, "track-step-race")

    def test_ota_identity(self):
        self.run_case(2, "identity-ready")
        self.run_case(3, "identity-race")

    def test_unknown_cleanup(self):
        self.run_case(4, "unknown-ready")
        self.run_case(5, "unknown-race")

    def test_unconditional_ota_poll_is_rejected(self):
        unsafe, count = re.subn(r"if\s*\(\s*ed_service\(\)\s*\)", "ed_service();", self.services)
        self.assertEqual(count, 1, "could not mutate the actual editor/OTA decision")
        mutant = self.source.replace(self.services, unsafe, 1)
        executable, _, _ = self.compile_source(mutant, "unconditional-ota")
        results = []
        for case, cmd, argument in ((0, 5, 29), (1, 30, 50)):
            with self.subTest(case=case):
                result = subprocess.run([str(executable), str(case)], capture_output=True, text=True)
                (self.out / f"unconditional-ota-{case}.log").write_text(result.stdout + result.stderr)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertRegex(result.stdout, rf"cmd {cmd} \[0,{argument}\].*expected [1-9][0-9]* bytes, got 0")
                results.append({"case": case, "exit_code": result.returncode, "output": result.stdout})
        # The negative is a lost editor reply, not a generally broken OTA/USB fixture.
        for case in (2, 3, 4, 5):
            result = subprocess.run([str(executable), str(case)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            results.append({"case": case, "exit_code": result.returncode, "output": result.stdout})
        (self.out / "unconditional-ota-results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    unittest.main()
