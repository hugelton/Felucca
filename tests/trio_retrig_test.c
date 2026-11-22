/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* TRIO retrigger (#321): a note that retriggers a voice still in its release keeps the filter (engine_t.keep,
 * voice.c voice_start), so the output goes on without a jump; with CUT about half the filter's restart clicked. */
#define main hostsim_main
#include "hostsim.c"
#undef main

int main(void)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[0];
    vmod_t m = {0};
    int32_t a[CTL], b[CTL], steady = 0, jump, last;
    uint32_t i, k;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    for (i = 0; i < NENGINES; i++)
        if (ENGINES[i] == &ENG_TRIO)
            host_preset(t, i, 0);
    t->p[P_E5] = 64;                                    /* CUT about half, RES low */
    t->p[P_E6] = 10;
    m.inc = pitch_inc(48 * 16);
    m.amp0 = m.amp1 = m.envq15 = 32767;
    m.shape = 64 << 8;
    voice_start(t, v, 48, 100, 0);
    for (k = 0; k < 250; k++) {
        memset(a, 0, sizeof a);
        ENG_TRIO.render(t, v, a, CTL, &m);
        if (k >= 200)
            for (i = 1; i < CTL; i++)
                if (abs(a[i] - a[i - 1]) > steady)
                    steady = abs(a[i] - a[i - 1]);
    }
    last = a[CTL - 1];
    v->gate = 0;
    v->stage = 4;                                       /* in its release */
    voice_start(t, v, 48, 100, 0);                      /* the retrigger */
    memset(b, 0, sizeof b);
    ENG_TRIO.render(t, v, b, CTL, &m);
    jump = abs(b[0] - last);
    for (i = 1; i < 8; i++)
        if (abs(b[i] - b[i - 1]) > jump)
            jump = abs(b[i] - b[i - 1]);
    printf("trio: retrigger in the release: jump %d, the steady signal's largest step %d\n", (int)jump, (int)steady);
    if (jump > steady / 2) {
        printf("trio: FAIL: the retrigger jumps (the filter restarted)\n");
        return 1;
    }
    printf("trio: retrigger keeps the filter ok\n");
    return 0;
}
