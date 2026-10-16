<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Signed audio scaling and phase wrapping

Strict UBSan exposes left shifts of negative audio/feedback values in the DC
blocker, low-cut filter, FM6 velocity and sine-LFO scaling, and PHYS output.
Replace these bounded power-of-two scalings with multiplication. Keep the
existing arithmetic right shifts, error-feedback values and parameter ranges.

FM6 modulation and feedback can also add across the signed phase boundary.
Perform those additions through uint32_t before converting back to the signed
phase representation, matching the phase accumulator's existing unsigned wrap.
No presets, coefficients, voice allocation or persistence formats change.

`tests/signed_audio_scaling_test.c` freezes DC/low-cut remainders and FM6
velocity/LFO results, plus two phase-boundary audio hashes from the original
1.6.1 implementation. `tests/phys_signed_test.c` excites the actual PHYS track
and checks negative and positive samples against its original render hash.
Both run normally and under strict ASan/UBSan without exclusions or recovery.
The original cases reproduce sanitizer failures on the unchanged baseline.

These focused checks supplement the existing renders, target budgets and
one-core/two-core regressions. They do not claim every DSP path is free of
undefined behavior. LOFI belongs to the separate Felucca-Engines repository;
neither its source nor the engine submodule pin changes in this patch.
