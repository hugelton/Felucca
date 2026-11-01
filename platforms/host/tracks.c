/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Tracks set straight, without the UI (after platforms/host/firmware.c): the power-on defaults, a factory preset as
 * the UI loads it, the drums, a step. For the host tests (tests/hostsim.c) and the host's test song
 * (felucca_host.c fh_test_*). */
static void host_tracks_init(void)                /* as felucca_init: defaults, empty patterns */
{
    uint32_t i, k;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    song.g[G_RTYPE] = 0;                          /* ROOM, the power-on reverb until 1.2: the renders (goldens) as
                                                   * before, ROOM bit for bit; HALL, the default since: reverb_test.c */
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_E0; i++)
            trk[k].p[i] = TP[i].def;
        for (i = 0; i < NSTEP; i++)
            trk[k].step[i].time = ST_REST;
    }
    song.master_q12 = 4096;
}

/* the factory preset as ui.c apply_preset_to sets it (sound, sends; not the steps, not the track's own:
 * the mix, ARP, SCL, the pattern parameters, the SLICER);
 * the engine as the UI asks for it (eng_req: the audio side switches; host_preset: at once) */
static void host_preset_values(track_t *t, uint32_t e, uint32_t pi, const preset_t *p)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    uint32_t i;
    t->eng_req = (uint8_t)e;
    t->preset = (uint8_t)pi;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = p->e[i];
    t->p[P_ATK] = p->env[0];
    t->p[P_DEC] = p->env[1];
    t->p[P_SUS] = p->env[2];
    t->p[P_REL] = p->env[3];
    t->p[P_ED_FLT] = p->fenv;
    t->p[P_VOICE] = p->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(p->fx[i] ? p->fx[i] - 1 : FX_DEF[i]);
    fm6_track_loaded(t);                          /* FM6: the preset's patch (ui.c apply_preset_to) */
}
static void host_preset_req(track_t *t, uint32_t e, uint32_t pi)
{
    uint32_t n = ENGINES[e]->npresets;
#if !FELUCCA_FM4
    if (e == ENGI_DIGITAL) {                      /* DIGITAL preset pi (retired): converted, as ui.c fm4_load_preset */
        uint8_t v[FP_SIZE + 1u];
        uint32_t tr = (uint32_t)(t - trk) % NTRK;
        fm4_preset_values(t->p, pi);
        t->preset = (uint8_t)fm4_convert(t->p, v);
        t->eng_req = ENGI_FM6;
        fm6_set_patch(tr, v);
        fm6_slot[tr] = (uint8_t)t->p[P_E7];
        return;
    }
#endif
    if (!n) {
        t->eng_req = (uint8_t)e;
        t->preset = 0;
        for (uint32_t i = 0; i < 8u; i++)
            t->p[P_E0 + i] = ENGINES[e]->edit[i].def;
        return;
    }
    pi %= n;
    host_preset_values(t, e, pi, &ENGINES[e]->presets[pi]);
}
static void host_preset(track_t *t, uint32_t e, uint32_t pi)
{
    host_preset_req(t, e, pi);
    t->engine = t->eng_req;                       /* (DIGITAL without FELUCCA_FM4: FM6) */
    eng_mem_claim(t);                             /* (voice.c engine_block would at the next block) */
}

/* A saved SAMPLE PERC sound, as 1.0.2 and earlier stored it (SET 4, the GM kit; retired since: every load
 * turns it into DRUM, core.h drum_from_perc). Put straight into a track it plays SET 4's alias (PIANO): only
 * for the migration tests, which pass it through a load */
static void host_legacy_sample_perc(track_t *t)
{
    static const preset_t PERC = {"PERC", {SMP_SET_PERC, 0, 0, 0, 127, 0, 0, 0}, {0, 127, 127, 60}, 0, 0, PAT(12)};
    host_preset_values(t, ENGI_SAMPLE, SMP_SET_PERC, &PERC);
    t->engine = ENGI_SAMPLE;
}
/* the drums of the render tests: DRUM's kit (the power-on track 4; until 1.0.2 these used SAMPLE PERC) */
static void host_drums(track_t *t) { host_preset(t, ENGI_DRUM, 0); }

static void put_step(track_t *t, uint32_t i, uint32_t n, const uint8_t *notes, uint32_t time, uint32_t flags)
{
    step_t *s = &t->step[i];
    uint32_t k;
    s->n = (uint8_t)n;
    for (k = 0; k < 4u; k++)
        s->note[k] = k < n ? notes[k] : 0;
    s->time = (uint8_t)time;
    s->flags = (uint8_t)flags;
    s->vel = n ? 100 : 0;
}
