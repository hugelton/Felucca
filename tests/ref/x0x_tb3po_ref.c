/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* X0X's TB-3PO as the reference of tests/tb3po_test.c: tb3po.c, tb3po.h and pattern.h here are X0X's, as they are
 * (github.com/charlesvestal/fm1-x0x at 54194c5, firmware/src/seq/, GPL-3.0-only), with their floats. Its own translation
 * unit (its names are X0X's), plain types across: a line is notes[64] and flags[64] (gate | 4 accent | 8 slide, the
 * same bits as Felucca's tb3po.c); cfg the eight settings in tb3po_cfg_t's order. Built with -ffp-contract=off */
#include "tb3po.c"

static void ref_cfg(bpart_t *b, const uint8_t *cfg, uint32_t seed, int len)
{
    b->gen.density = cfg[0];
    b->gen.accent = cfg[1];
    b->gen.slide = cfg[2];
    b->gen.oct_range = cfg[3];
    b->gen.root = cfg[4];
    b->gen.scale = cfg[5];
    b->gen.base_oct = cfg[6];
    b->gen.mutate_bars = cfg[7];
    b->gen.seed = seed;
    b->len = (uint8_t)len;
}

void ref_generate(const uint8_t *cfg, uint32_t seed, int len, uint8_t *notes, uint8_t *flags)
{
    static bpart_t b;
    int i;
    ref_cfg(&b, cfg, seed, len);
    tb3po_generate(&b);
    for (i = 0; i < NSTEPS; i++) {
        notes[i] = b.step[i].note;
        flags[i] = b.step[i].flags;
    }
}

void ref_mutate(const uint8_t *cfg, uint32_t seed, int len, uint8_t *notes, uint8_t *flags, uint32_t *rng)
{
    static bpart_t b;
    int i;
    ref_cfg(&b, cfg, seed, len);
    for (i = 0; i < NSTEPS; i++) {
        b.step[i].note = notes[i];
        b.step[i].flags = flags[i];
    }
    tb3po_mutate(&b, rng);
    for (i = 0; i < NSTEPS; i++) {
        notes[i] = b.step[i].note;
        flags[i] = b.step[i].flags;
    }
}

uint32_t ref_new_seed(uint32_t entropy) { return tb3po_new_seed(entropy); }

void ref_defaults(uint8_t *cfg, uint32_t *seed)
{
    tb3po_cfg_t g;
    tb3po_defaults(&g, 0);
    cfg[0] = g.density; cfg[1] = g.accent; cfg[2] = g.slide; cfg[3] = g.oct_range;
    cfg[4] = g.root; cfg[5] = g.scale; cfg[6] = g.base_oct; cfg[7] = g.mutate_bars;
    *seed = g.seed;
}
