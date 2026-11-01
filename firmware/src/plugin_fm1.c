/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM-1's plugin engines (FELUCCA_PLUGINS): blobs in the flash after the app's area (Felucca-Engines
 * felucca_plugin.h FE_FM1_*: two slots and the list of plugin numbers, 0x93000 .. 0x96FFF, which an update of the
 * firmware never writes), each linked for its slot (tools/build_plugin_fm1.sh). At power-on (plg_fm1_boot) a slot
 * whose header and image check out gets its .data and .bss, the PC limit lets the plugins' area run
 * (hal/fm1_plugin_hal.h), its felucca_plugin() is called and the engine taken (plugin.c plg_add). A plugin of its
 * own plays as the number the list gives its id (the editor's PLUGIN REG wrote it); none: it is not taken.
 * A crash in the last boot inside a plugin (plugin.c plg_depth, kept over the reset; or the crash record's pc or rets in
 * the plugins' area): no plugin this boot.
 * The editor's commands (editor_plugin.c) list, erase and write the slots, register a number and restart. */
#include "fm1_plugin_hal.h"
#include "../engines/src/felucca_plugin_fm1.h"

#define PLG_FM1_LO FE_FM1_SLOT_FLASH(0)
#define PLG_FM1_HI (FE_FM1_REG + 0x200u)               /* 0x97000: the end of the area (the XDATA0 of felucca-bsp) */
_Static_assert(PLG_FM1_HI == 0x97000u && NPLUG >= FE_FM1_NSLOT, "the FM-1's plugin area");

enum { PLS_EMPTY, PLS_OK, PLS_BAD, PLS_REFUSED, PLS_SKIPPED, PLS_NONUM };
static uint8_t plg_fm1_st[FE_FM1_NSLOT];               /* PLS_* of each slot at this boot */
static uint8_t plg_fm1_no[FE_FM1_NSLOT];               /* the number it plays as (PLS_OK) */
static uint8_t plg_fm1_open;                           /* the PC limit lets the plugins' area run */
static uint32_t plg_fm1_restart;                       /* fm1_ms to restart at (editor_plugin.c), 0 = none */
/* a plugin was called: the second core (fx.c) stays off, its own PC limits would stop at a plugin's code */
#define PLG_FM1_RUNNING plg_fm1_open

static int plg_fm1_in(uint32_t a) { return a >= FM1_XIP(PLG_FM1_LO) && a < FM1_XIP(PLG_FM1_HI); }
static const fe_fm1_hdr_t *plg_fm1_hdr(uint32_t k) { return (const fe_fm1_hdr_t *)fm1_xip_ptr(FE_FM1_SLOT_FLASH(k)); }

/* the number of plugin id in the list (FE_FM1_REG), 0 = none */
static uint32_t plg_fm1_number(uint32_t id)
{
    const uint32_t *r = (const uint32_t *)fm1_xip_ptr(FE_FM1_REG);
    uint32_t k;
    for (k = 0; k < FE_FM1_REG_N; k++)
        if (r[k] == id)
            return ENGI_PLUG0 + k;
    return 0;
}

static uint32_t plg_fm1_reg_id(uint32_t no)            /* the id the list keeps for number no, 0 = none */
{
    uint32_t id = no >= ENGI_PLUG0 && no - ENGI_PLUG0 < FE_FM1_REG_N ?
                  ((const uint32_t *)fm1_xip_ptr(FE_FM1_REG))[no - ENGI_PLUG0] : 0u;
    return id == 0xFFFFFFFFu ? 0u : id;
}

/* slot k's header and image as the loader takes them: 1 good */
static int plg_fm1_ok(uint32_t k)
{
    const fe_fm1_hdr_t *h = plg_fm1_hdr(k);
    uint32_t ram = FE_FM1_SLOT_RAM(k), x = FM1_XIP(FE_FM1_SLOT_FLASH(k)) + sizeof *h;
    return h->magic == FE_FM1_MAGIC && h->hdr_ver == 1u && h->hdr_size == sizeof *h && h->platform == FE_FM1_PLATFORM &&
           h->abi == FE_ABI && h->slot == k && h->xip == x && h->image_len <= FE_FM1_SLOT_SIZE(k) - sizeof *h &&
           h->entry >= x && h->entry < x + h->image_len &&
           h->data_len <= FE_FM1_SLOT_RAMSIZE && h->bss_len <= FE_FM1_SLOT_RAMSIZE &&
           (!h->data_len || (h->data_off <= h->image_len - h->data_len && h->data_addr >= ram &&
                             h->data_addr + h->data_len <= ram + FE_FM1_SLOT_RAMSIZE)) &&
           (!h->bss_len || (h->bss_addr >= ram && h->bss_addr + h->bss_len <= ram + FE_FM1_SLOT_RAMSIZE)) &&
           st_crc32((const uint8_t *)(uintptr_t)x, h->image_len) == h->image_crc;
}

/* power-on, before the parts get their sounds; crashed: the last boot ended in a crash (the record is new) */
static void plg_fm1_boot(int crashed)
{
    uint32_t k, i, no;
    int skip = crashed && (plg_depth || plg_fm1_in(fm1_crash.pc) || plg_fm1_in(fm1_crash.rets));
    plg_depth = 0;
    if (!flash_ok)
        return;
    plg_reg_id = plg_fm1_reg_id;                       /* (a project's plugin found by its id: project.c) */
    plg_reg_no = plg_fm1_number;
    for (k = 0; k < FE_FM1_NSLOT; k++) {
        const fe_fm1_hdr_t *h = plg_fm1_hdr(k);
        const fe_plugin_t *d;
        if (h->magic == 0xFFFFFFFFu) {
            plg_fm1_st[k] = PLS_EMPTY;
            continue;
        }
        if (!plg_fm1_ok(k)) {
            plg_fm1_st[k] = PLS_BAD;
            continue;
        }
        if (skip) {
            plg_fm1_st[k] = PLS_SKIPPED;
            continue;
        }
        for (i = 0; i < h->data_len; i++)
            ((uint8_t *)(uintptr_t)h->data_addr)[i] = fm1_xip_ptr(FE_FM1_SLOT_FLASH(k))[sizeof *h + h->data_off + i];
        for (i = 0; i < h->bss_len; i++)
            ((uint8_t *)(uintptr_t)h->bss_addr)[i] = 0;
        if (!plg_fm1_open) {
            fm1_pc_code_up_to(FM1_XIP(PLG_FM1_HI) - 1u);
            plg_fm1_open = 1;
        }
        PLG_IN();
        d = ((fe_entry_t)(uintptr_t)h->entry)(&PLG_HOST);
        PLG_OUT();
        no = d && d->engine == FE_NEW ? plg_fm1_number(h->id) : 0u;
        if (d && d->id != h->id)
            d = 0;
        if (d && d->engine == FE_NEW && !no)
            plg_fm1_st[k] = PLS_NONUM;
        else if (!d || plg_add(d, no))
            plg_fm1_st[k] = PLS_REFUSED;
        else {
            plg_fm1_st[k] = PLS_OK;
            plg_fm1_no[k] = (uint8_t)(d->engine == FE_NEW ? no : d->engine);
        }
    }
}

/* slot k's plugin out of the engines (before its flash is erased): its number plays what it was before */
static void plg_fm1_drop(uint32_t k)
{
    if (k < FE_FM1_NSLOT && plg_fm1_st[k] == PLS_OK) {
        plg_remove(plg_fm1_no[k]);
        plg_fm1_st[k] = PLS_EMPTY;
    }
}
