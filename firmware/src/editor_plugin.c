/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The plugin engines' slots for the editor (plugin_fm1.c; Felucca-Engines felucca_plugin.h FE_FM1_*). A 32-bit
 * value goes as five 7-bit bytes, a flash offset in a slot as three, LSB first.
 *   PLUGIN_INFO     -> FE_ABI, slots, per slot: state (plugin_fm1.c PLS_*: 0 empty, 1 playing, 2 not a good blob,
 *                      3 refused, 4 skipped after a crash, 5 no number), id, version, the number it plays as (127 = a
 *                      number of its own not given), its name; then the numbers given: count, per one its number -
 *                      ENGI_PLUG0 and the id
 *   PLUGIN_ERASE    slot -> slot, rc: the slot's plugin out of the engines (its number plays what it did before), its
 *                      flash erased (the numbers' list kept)
 *   PLUGIN_WRITE    slot, off, pack7 data (a page: off a multiple of 256, at most 256 bytes) -> slot, off, rc; into an
 *                      erased slot. The blob (tools/fep_pack.py) as it is; the slot plays it from the next power-on
 *   PLUGIN_REG      id -> number - ENGI_PLUG0, rc: the number of a plugin of its own, given once and kept
 *   PLUGIN_RESTART  -> 0: the FM-1 restarts (the plugins load at power-on)
 *   PLUGIN_ENGINES  -> count, per engine of a plugin listed after Felucca's (the ENG knob's order): its number, its
 *                      name; INFO's tag 58 01 n says how many (the engines' parameters and presets: DESC, NAMES)
 * rc: 0 done, 1 bad arguments, 2 the flash failed, 3 the slot is playing (erase it first), 4 the list is full,
 * 5 an update record's mark at a sector's 0xF06 (fm1_flash.h) */
enum { ED_PLUGIN_INFO = 82, ED_PLUGIN_ERASE, ED_PLUGIN_WRITE, ED_PLUGIN_REG, ED_PLUGIN_RESTART, ED_PLUGIN_ENGINES };

static void ed_u32(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        ed_b(v >> (7u * i));
}
static uint32_t ed_get_u32(const uint8_t *a)
{
    return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
}

static uint32_t ed_plugin_erase(uint32_t k)
{
    uint32_t s, took, f = FE_FM1_SLOT_FLASH(k), end = f + FE_FM1_SLOT_SIZE(k);
    if (ed_flash_stop())
        return 2;
    plg_fm1_drop(k);
    for (s = f & ~0xFFFu; s < end; s += 0x1000u) {
        uint32_t keep = s <= FE_FM1_REG && FE_FM1_REG < s + 0x1000u, i, any = 0;
        if (keep) {                                    /* the numbers' list shares the sector: kept */
            for (i = 0; i < 4u * FE_FM1_REG_N; i++)
                any |= (ed_smp_buf[i] = fm1_xip_ptr(FE_FM1_REG)[i]) != 0xFFu;
        }
        audio_silence();
        if (fl_erase4k(s, &took))
            return 2;
        if (keep && any && fl_write(FE_FM1_REG, ed_smp_buf, 4u * FE_FM1_REG_N))
            return 2;
        fm1_wdt_feed();
    }
    return 0;
}

static uint32_t ed_plugin_write(uint32_t k, uint32_t off, const uint8_t *a, uint32_t na)
{
    uint32_t len = ed_unpack7(a, na, ed_smp_buf, 256u), f = FE_FM1_SLOT_FLASH(k), i;
    if (!len || (off & 0xFFu) || off + len > FE_FM1_SLOT_SIZE(k))
        return 1;
    if (plg_fm1_st[k] == PLS_OK)
        return 3;
    for (i = 0; i < len; i++)                          /* bytes 0xF06, 0xF07 of a sector: never "AT" */
        if (((f + off + i) & 0xFFFu) == 0xF06u && i + 1u < len && ed_smp_buf[i] == 'A' && ed_smp_buf[i + 1u] == 'T')
            return 5;
    if (ed_flash_stop() || fl_write(f + off, ed_smp_buf, len))
        return 2;
    return 0;
}

/* the number of id: kept, or the first free entry written; *n its index */
static uint32_t ed_plugin_reg(uint32_t id, uint32_t *n)
{
    const uint32_t *r = (const uint32_t *)fm1_xip_ptr(FE_FM1_REG);
    uint32_t k, v = id;
    if (id == 0xFFFFFFFFu || !plg_id_ok(id))
        return 1;
    for (k = 0; k < FE_FM1_REG_N && r[k] != id; k++)
        ;
    if (k < FE_FM1_REG_N) {
        *n = k;
        return 0;
    }
    for (k = 0; k < FE_FM1_REG_N - 1u && r[k] != 0xFFFFFFFFu; k++)   /* (the last number is ENGI_UNKNOWN's) */
        ;
    if (k == FE_FM1_REG_N - 1u)
        return 4;
    *n = k;
    return fl_write(FE_FM1_REG + 4u * k, (const uint8_t *)&v, 4u) ? 2u : 0u;
}

static int ed_plugin_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t k, c, rc;
    if (cmd < ED_PLUGIN_INFO || cmd > ED_PLUGIN_ENGINES)
        return 0;
    if (cmd == ED_PLUGIN_ENGINES) {
        ed_b(eng_nshown() - NENG_SHOWN);
        for (k = NENG_SHOWN; k < eng_nshown(); k++) {
            ed_b(eng_vis(k));
            ed_str(ENGINES[eng_vis(k)]->name, 8);
        }
        return 1;
    }
    if (!flash_ok) {
        ed_b(1);
        return 1;
    }
    switch (cmd) {
    case ED_PLUGIN_INFO: {
        const uint32_t *r = (const uint32_t *)fm1_xip_ptr(FE_FM1_REG);
        ed_b(FE_ABI);
        ed_b(FE_FM1_NSLOT);
        for (k = 0; k < FE_FM1_NSLOT; k++) {
            const fe_fm1_hdr_t *h = plg_fm1_hdr(k);
            char nm[17] = {0};
            uint32_t i, good = plg_fm1_st[k] != PLS_EMPTY && plg_fm1_st[k] != PLS_BAD;
            ed_b(plg_fm1_st[k]);
            ed_u32(good ? h->id : 0u);
            ed_u32(good ? h->version : 0u);
            ed_b(plg_fm1_st[k] == PLS_OK ? plg_fm1_no[k] : good && h->engine < 127u ? h->engine : 127u);
            for (i = 0; good && i < 16u; i++)
                nm[i] = h->name[i] >= 32 && h->name[i] < 127 ? h->name[i] : 0;
            ed_str(nm, 16);
        }
        for (k = c = 0; k < FE_FM1_REG_N; k++)
            c += r[k] != 0xFFFFFFFFu;
        ed_b(c);
        for (k = 0; k < FE_FM1_REG_N; k++)
            if (r[k] != 0xFFFFFFFFu) {
                ed_b(k);
                ed_u32(r[k]);
            }
        return 1;
    }
    case ED_PLUGIN_ERASE:
        k = n == 1u ? a[0] : FE_FM1_NSLOT;
        ed_b(k);
        ed_b(k < FE_FM1_NSLOT ? ed_plugin_erase(k) : 1u);
        return 1;
    case ED_PLUGIN_WRITE: {
        uint32_t off = n >= 5u ? (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14 : 0u;
        k = n >= 5u ? a[0] : FE_FM1_NSLOT;
        rc = k < FE_FM1_NSLOT ? ed_plugin_write(k, off, a + 4, n - 4u) : 1u;
        ed_b(k);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_b(rc);
        return 1;
    }
    case ED_PLUGIN_REG:
        k = 0;
        rc = n == 5u ? ed_plugin_reg(ed_get_u32(a), &k) : 1u;
        ed_b(k);
        ed_b(rc);
        return 1;
    default:                                           /* ED_PLUGIN_RESTART */
        plg_fm1_restart = (fm1_ms + 300u) | 1u;        /* (after the reply has gone) */
        ed_b(0);
        return 1;
    }
}
