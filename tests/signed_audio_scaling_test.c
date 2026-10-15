/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed arithmetic vectors from 1.6.1, also run with strict UBSan. Negative
 * DC/lowcut feedback and FM6 velocity scaling must retain their exact values. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t failures;
static void expect(const char *name, int ok)
{
    printf("signed scaling: %-58s %s\n", name, ok ? "PASS" : "FAIL");
    failures += !ok;
}
int main(int argc, char **argv)
{
    if (!(argc == 2 && !strcmp(argv[1], "--fm6"))) {
        int32_t dc = 10000, err = 0;
        int32_t out = dc_block(0, &dc, &err);
        expect("DC negative feedback remainder", out == -156 && dc == 9997 && err == 2288);
        dc = err = 0; out = dc_block(-1, &dc, &err);
        expect("DC negative input preserves error feedback", out == -1 && dc == -1 && err == 4032);
        dc = err = 0; out = dc_block(-32768, &dc, &err);
        expect("DC negative full-scale input", out == -32760 && dc == -512 && err == 0);
        dc = 100; err = 0; out = lowcut1(0, &dc, &err, 4);
        expect("lowcut negative feedback remainder", out == -93 && dc == 93 && err == 12);
        dc = err = 0; out = lowcut1(-32768, &dc, &err, 6);
        expect("lowcut negative full-scale input", out == -32256 && dc == -512 && err == 0);
    }
    expect("FM6 zero velocity / maximum sensitivity", fm6_scale_vel(0, 7) == -3344);
    expect("FM6 upper velocity / maximum sensitivity", fm6_scale_vel(127, 7) == 224);
    expect("FM6 velocity inputs clamped", fm6_scale_vel(-10, 7) == -3344 && fm6_scale_vel(130, 7) == 224);
    expect("FM6 zero sensitivity", fm6_scale_vel(0, 0) == 0 && fm6_scale_vel(127, 0) == 0);
    int32_t out[FM6_N] = {0}, in[FM6_N];
    for (uint32_t i = 0; i < FM6_N; i++) in[i] = 100000;
    fm6_op_run(out, in, INT32_MAX - 100, 1234, 1 << 20, 1 << 20, 0);
    uint64_t h = UINT64_C(14695981039346656037);
    for (uint32_t i = 0; i < FM6_N; i++) h = (h ^ (uint32_t)out[i]) * UINT64_C(1099511628211);
    expect("FM6 modulated phase crosses signed boundary", h == UINT64_C(0xedef9f78c8ca7e65));
    int32_t fb[2] = {100000, 100000};
    fm6_op_fb(out, INT32_MAX - 100, 1234, 1 << 20, 1 << 20, fb, 0, 0);
    h = UINT64_C(14695981039346656037);
    for (uint32_t i = 0; i < FM6_N; i++) h = (h ^ (uint32_t)out[i]) * UINT64_C(1099511628211);
    expect("FM6 feedback phase crosses signed boundary", h == UINT64_C(0x496218ec62e956e5));
    fm6_lfo_t l = {0}; l.wave = 4; l.phase = 0xc0000000u;
    expect("FM6 sine LFO negative half", fm6_lfo_sample(&l) == 256);
    puts(failures ? "SIGNED AUDIO SCALING FAILED" : "SIGNED AUDIO SCALING PASSED");
    return failures != 0;
}
