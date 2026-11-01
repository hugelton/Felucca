/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Plugin engines (FELUCCA_PLUGINS): engines built on their own against Felucca-Engines' felucca_plugin.h (MIT),
 * which a platform loads at run time (platforms/common/plugins.c: shared libraries) and hands to plg_add before
 * the firmware boots. A plugin plays as one of the engine numbers: one of Felucca's own (it takes that engine's
 * place in ENGINES[], so a sound saved with it plays through the plugin) or a number of its own (FE_NEW: ENGI_PLUG0
 * and up, which the platform keeps for the plugin's id, so a sound saved with it finds it again; listed after
 * Felucca's engines, eng_nshown). A number no plugin plays is ENG_MISSING: a sine and NO PLUGIN.
 *
 * The plugin sees a part through fe_track_t, never the track: plg_view copies the eleven parameters it reads into
 * the part's view before each call (the fade of an engine switch has put the old engine's values in t->p, so the
 * view has them too), with the part's voices and engine memory. Each slot has its own engine_t: the plugin's
 * names, parameters and presets, its knobs in the track's parameter numbers, and the functions below, which call
 * the plugin's with the view. Felucca's code calls the engines as it always did.
 *
 * A plugin may be under any licence: LICENSING.md, "Plugin engines" (an additional permission under GPL-3.0 section 7
 * for engines that use only this interface). docs/PLUGINS.md and docs/ARCHITECTURE.md describe the whole. */
#if FELUCCA_PLUGINS
#define FE_HOST
#include "../engines/src/felucca_plugin.h"

_Static_assert(NPLUG <= 8u, "plugin.c has eight slots' functions");
typedef struct {
    const fe_plugin_t *d;        /* 0 = a free slot */
    engine_t e;                  /* what ENGINES[no] points at */
    const engine_t *was;         /* the engine it took the place of */
    uint8_t no;
} plg_t;
static plg_t plg[NPLUG];
static fe_track_t plg_views[NPART];
static const uint8_t PLG_P[FE_NP] = {P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7};

static struct track *plg_view(const track_t *t)
{
    uint32_t part = (uint32_t)(t - trk) % NPART, k;
    fe_track_t *w = &plg_views[part];
    for (k = 0; k < FE_NP; k++)
        w->p[k] = t->p[PLG_P[k]];
    w->part = (uint16_t)part;
    w->v = (voice_t *)t->v;
    w->mem = ENG_MEM(part);
    return (struct track *)w;
}

/* each call into a plugin is counted while it runs: on the FM-1 in .noinit, so a crash inside one is known at the next
 * power-on (plugin_fm1.c plg_fm1_boot); the desktop builds count nothing */
#ifdef FELUCCA_HOST
#define PLG_IN() ((void)0)
#define PLG_OUT() ((void)0)
#else
static volatile uint32_t plg_depth __attribute__((section(".noinit")));
#define PLG_IN() (plg_depth++)
#define PLG_OUT() (plg_depth--)
#endif
#define PLG_E(i) (plg[i].d->e)
#define PLG_V(i, call) do { PLG_IN(); PLG_E(i)->call; PLG_OUT(); } while (0)
#define PLG_R(i, type, call) do { type r_; PLG_IN(); r_ = PLG_E(i)->call; PLG_OUT(); return r_; } while (0)
#define PLG_SLOT(i)                                                                                            \
    static void plg_on##i(struct track *t, voice_t *v) { PLG_V(i, note_on(plg_view(t), v)); }                \
    static void plg_render##i(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)       \
    {                                                                                                          \
        PLG_V(i, render(plg_view(t), v, out, n, m));                                                           \
    }                                                                                                          \
    static int32_t plg_amp##i(struct track *t, voice_t *v, int32_t a) { PLG_R(i, int32_t, amp(plg_view(t), v, a)); } \
    static const param_desc_t *plg_desc##i(const struct track *t, uint32_t k)                                 \
    {                                                                                                          \
        PLG_R(i, const param_desc_t *, desc(plg_view(t), k));                                                  \
    }                                                                                                          \
    static void plg_block##i(struct track *t) { PLG_V(i, block(plg_view(t))); }                               \
    static int32_t plg_keys##i(const struct track *t, uint32_t k) { PLG_R(i, int32_t, keys(plg_view(t), k)); } \
    static int plg_done##i(struct track *t, voice_t *v) { PLG_R(i, int, done(plg_view(t), v)); }              \
    static uint32_t plg_post##i(struct track *t, int32_t *out, uint32_t n) { PLG_R(i, uint32_t, post(plg_view(t), out, n)); }
PLG_SLOT(0) PLG_SLOT(1) PLG_SLOT(2) PLG_SLOT(3) PLG_SLOT(4) PLG_SLOT(5) PLG_SLOT(6) PLG_SLOT(7)
#define PLG_FNS(i) {plg_on##i, plg_render##i, plg_amp##i, plg_desc##i, plg_block##i, plg_keys##i, plg_done##i, plg_post##i}
static const struct {
    void (*note_on)(struct track *, voice_t *);
    void (*render)(struct track *, voice_t *, int32_t *, uint32_t, const vmod_t *);
    int32_t (*amp)(struct track *, voice_t *, int32_t);
    const param_desc_t *(*desc)(const struct track *, uint32_t);
    void (*block)(struct track *);
    int32_t (*keys)(const struct track *, uint32_t);
    int (*done)(struct track *, voice_t *);
    uint32_t (*post)(struct track *, int32_t *, uint32_t);
} PLG_FN[8] = {PLG_FNS(0), PLG_FNS(1), PLG_FNS(2), PLG_FNS(3), PLG_FNS(4), PLG_FNS(5), PLG_FNS(6), PLG_FNS(7)};

/* what a plugin's felucca_plugin() gets: the views and Felucca's tables (a plugin built with --shared uses these) */
#ifndef FELUCCA_PLATFORM
#define FELUCCA_PLATFORM FE_PLATFORM_FM1         /* (a host platform's build sets its FE_PLATFORM_*) */
#endif
static const struct fe_tables PLG_TABLES = {SINE, PITCH_OCT, SVF_G, TANH_Q15};
static const fe_host_t PLG_HOST = {FE_ABI, sizeof(fe_host_t), FELUCCA_PLATFORM, plg_views, &PLG_TABLES, {0, 0, 0, 0}};

/* the start of a plugin's engine_t (n bytes as it was built, the fields this host has not 0) */
static engine_t plg_e;
static void plg_ecopy(engine_t *to, const engine_t *from, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < sizeof *to; k++)
        ((uint8_t *)to)[k] = k < n ? ((const uint8_t *)from)[k] : 0u;
}

/* The numbers of plugins of their own are a device's (its platform keeps one for each id: the FM-1's list, a desktop's
 * felucca_plugins.txt), so a project keeps the id of a track's plugin beside its number (project.c proj_plugin_*): on
 * another device it plays as the number that device has for the id. The platform tells the ids it has given numbers
 * (plg_reg_id / plg_reg_no, 0 = none), installed or not; the plugins playing say theirs too */
static uint32_t (*plg_reg_id)(uint32_t no);      /* the id the platform keeps for number no, 0 = none */
static uint32_t (*plg_reg_no)(uint32_t id);      /* the number it keeps for id, 0 = none */
static uint32_t plg_track_id[NTRK];              /* a track on ENGI_UNKNOWN: the id it was saved with */

static uint32_t plg_id_of(uint32_t no)           /* number no's plugin id, 0 = none */
{
    uint32_t s;
    if (no < ENGI_PLUG0 || no >= ENGI_UNKNOWN)
        return 0;
    for (s = 0; s < NPLUG; s++)
        if (plg[s].d && plg[s].no == no)
            return plg[s].d->id;
    return plg_reg_id ? plg_reg_id(no) : 0u;
}
static uint32_t plg_no_of(uint32_t id)           /* plugin id's number here, 0 = none */
{
    uint32_t s, no;
    for (s = 0; s < NPLUG; s++)
        if (plg[s].d && plg[s].d->id == id && plg[s].no >= ENGI_PLUG0)
            return plg[s].no;
    no = plg_reg_no ? plg_reg_no(id) : 0u;
    return no >= ENGI_PLUG0 && no < ENGI_UNKNOWN ? no : 0u;
}
/* an id a project can keep: four printable characters (FE_ID) */
static int plg_id_ok(uint32_t id)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (((id >> (8u * k)) & 0xFFu) < 32u || ((id >> (8u * k)) & 0xFFu) > 126u)
            return 0;
    return 1;
}

/* takes plugin d (before the firmware boots), as number no when it asks for one of its own (FE_NEW): 0 done, else why
 * not (1 not a plugin of this firmware, 2 its number is not one it can play as, 3 no free slot) */
static int plg_add(const fe_plugin_t *d, uint32_t no)
{
    uint32_t s, k;
    const engine_t *x;
    plg_t *g;
    if (!d || d->magic != FE_MAGIC || d->abi != FE_ABI || d->size < FE_PLUGIN_SIZE_1 || d->fs != FS || d->ctl != CTL ||
        d->nvoice != NVOICE || d->npart != NPART || !d->e || d->e_size < FE_ENGINE_SIZE_1 || !plg_id_ok(d->id))
        return 1;
    plg_ecopy(&plg_e, d->e, d->e_size);             /* (a plugin's engine_t of a later ABI 1 is longer: the start) */
    x = &plg_e;
    if (!x->note_on || !x->render || x->mem > ENG_MEM_PART ||
        (x->ownenv && !x->done) || !x->name || !x->page_title[0] || !x->page_title[1] || !x->presets ||
        !x->npresets)
        return 1;
    for (k = 0; k < 8u; k++)                           /* (the UI draws every label and preset name) */
        if (!x->edit[k].label || (x->edit[k].fmt == F_ENUM && !x->edit[k].names))
            return 1;
    for (k = 0; k < x->npresets; k++)
        if (!x->presets[k].name)
            return 1;
    /* Felucca's own numbers, but DRUM (its grid and lanes are the firmware's) and DIGITAL's reserved one; or a number
     * of the plugins' range */
    if (d->engine != FE_NEW)
        no = d->engine;
    if (no < NENG_BUILTIN ? no == ENGI_DRUM || !eng_ok(no) : no < ENGI_PLUG0 || no >= ENGI_UNKNOWN)
        return 2;
    for (s = 0; s < NPLUG && plg[s].d && plg[s].no != no; s++)
        ;
    if (s == NPLUG)
        return 3;
    g = &plg[s];
    if (!g->d) {
        g->was = ENGINES[no];
        if (no >= ENGI_PLUG0) {                        /* listed after Felucca's engines, by number */
            for (k = eng_nshown(); k > NENG_SHOWN && eng_order[k - 1u] > no; k--)
                eng_order[k] = eng_order[k - 1u];
            eng_order[k] = (uint8_t)no;
            eng_nvis++;
        }
    }
    g->d = d;
    g->no = (uint8_t)no;
    g->e = *x;
    for (k = 0; k < 4u; k++)                           /* the knobs: FE_P_* -> the track's numbers */
        g->e.knob[k] = x->knob[k] < FE_NP ? PLG_P[x->knob[k]] : P_E0;
    g->e.note_on = PLG_FN[s].note_on;
    g->e.render = PLG_FN[s].render;
    g->e.amp = x->amp ? PLG_FN[s].amp : 0;
    g->e.desc = x->desc ? PLG_FN[s].desc : 0;
    g->e.block = x->block ? PLG_FN[s].block : 0;
    g->e.keys = x->keys ? PLG_FN[s].keys : 0;
    g->e.done = x->done ? PLG_FN[s].done : 0;
    g->e.post = x->post ? PLG_FN[s].post : 0;
    ENGINES[no] = &g->e;
    return 0;
}

/* the plugin playing as number no out (its code is to go: FM-1 plugin_fm1.c): the number plays what it did before it
 * came (Felucca's engine, or ENG_MISSING) and leaves the list */
static void plg_remove(uint32_t no)
{
    uint32_t s, k;
    for (s = 0; s < NPLUG && !(plg[s].d && plg[s].no == no); s++)
        ;
    if (s == NPLUG)
        return;
    ENGINES[no] = plg[s].was;
    plg[s].d = 0;
    for (k = NENG_SHOWN; no >= ENGI_PLUG0 && k < eng_nshown(); k++)
        if (eng_order[k] == no) {
            for (; k + 1u < eng_nshown(); k++)
                eng_order[k] = eng_order[k + 1u];
            eng_nvis--;
            break;
        }
}
#endif
