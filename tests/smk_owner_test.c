/* SPDX-License-Identifier: GPL-3.0-only
 * Felucca on the SMK-37 Pro (BOARD_SMK37) */
/* SMK-37 Pro store owner tag (storage.c st_owner_check, build with -DBOARD_SMK37=1): a simulated flash with
 * another firmware's store, an untagged one and Felucca's own; only FL_DATA objects, the voice bank and the tag
 * sector are ever erased, and ours is kept. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif
#include "fm1_flash_map.h"

static uint8_t nor[0x100000];
static int bad_write;
static int tag_erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if ((off & 0xFFFu) || !(FL_IN(off, 0x1000u, 0x9D000u, 0xAF000u) || off == 0xAF000u || off == 0xB0000u || off == 0xEA000u))
        bad_write++;
    if (off == 0xEA000u)
        tag_erases++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off != 0xAF000u)
        bad_write++;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"

static int bad;
static void check(const char *what, int ok) { printf("smk owner: %-74s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static void fill(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(nor + 0x9D000, 0x11, 0x12000);          /* objects */
    memset(nor + 0xB0000, 0x22, 0x1000);           /* voice bank */
    memset(nor + 0xC1000, 0x33, 0x1000);           /* USRTRIM: never touched */
    memset(nor + 0xC2000, 0x44, 0x1000);           /* a user sample */
}
static int store_erased(void)
{
    uint32_t i;
    for (i = 0x9D000; i < 0xAF000; i++) if (nor[i] != 0xFF) return 0;
    for (i = 0xB0000; i < 0xB1000; i++) if (nor[i] != 0xFF) return 0;
    return 1;
}
static int others_kept(void) { return nor[0xC1000] == 0x33 && nor[0xC1FFF] == 0x33 && nor[0xC2000] == 0x44; }

static void stock(void)                            /* the stock mapping as V016 writes it */
{
    uint32_t n;
    memset(nor + 0xC2000, 0xFF, 0x29000);
    memset(nor + 0xC2000, 0x07, 0x86);
    nor[0xC2000] = 0xA3; nor[0xC2001] = 0x3A;
    for (n = 0; n < 8u; n++) {
        memset(nor + 0xC3000 + n * 0x5000, 0x01, 0x29E3);
        nor[0xC3000 + n * 0x5000 + 4] = 0x7F;
    }
    memcpy(nor + 0xEAFF8, "usrdata", 8);
}
static int is_stock(void)
{
    static uint8_t keep[0x29000];
    int same;
    memcpy(keep, nor + 0xC2000, sizeof keep);
    stock();
    same = !memcmp(keep, nor + 0xC2000, sizeof keep);
    memcpy(nor + 0xC2000, keep, sizeof keep);
    return same;
}
static int erased(uint32_t lo, uint32_t hi)
{
    for (; lo < hi; lo++)
        if (nor[lo] != 0xFF)
            return 0;
    return 1;
}

int main(void)
{
    fill();
    memcpy(nor + 0xAF000, "SMK37DATAOTHER\0", 16);
    st_owner_check();
    check("another firmware's store (another tag): cleared, tagged FELUCCA", store_erased() && st_owner_cleared &&
          !memcmp(nor + 0xAF000, "SMK37DATAFELUCCA", 16));
    check("... calibration (USRTRIM) and samples untouched", others_kept());
    fill();
    memcpy(nor + 0xAF000, "SMK37DATAFELUCCA", 16);
    st_owner_cleared = 0;
    st_owner_check();
    check("our own store: kept as it is", nor[0x9D000] == 0x11 && nor[0xB0000] == 0x22 && !st_owner_cleared);
    fill();
    st_owner_check();
    check("an untagged store (FELUCCA_DATA_UNTAGGED_OK 0): cleared, tagged", store_erased() &&
          !memcmp(nor + 0xAF000, "SMK37DATAFELUCCA", 16) && others_kept());
    /* the stock firmware's mapping in USRFLASH: kept while intact, its marker released over anything else */
    fill();
    stock();
    memcpy(nor + 0xAF000, "SMK37DATAOTHER\0", 16);
    tag_erases = 0;
    st_owner_check();
    check("owner change over the intact stock mapping: USRFLASH and its marker kept", is_stock() && tag_erases == 0 &&
          st_stock_intact());
    st_owner_check();
    check("... and at the next boot (ours now)", is_stock() && tag_erases == 0);
    st_stock_release();                            /* the first erase in USRFLASH (storage_hw.c, editor.c) */
    check("first write into USRFLASH: the marker sector erased, once", erased(0xEA000, 0xEB000) && tag_erases == 1 &&
          !st_stock_tagged() && nor[0xC2000] == 0xA3);
    st_stock_release();
    check("... and only once", tag_erases == 1);
    {
        static const uint32_t HIT[] = {0xC2000u, 0xC6000u, 0xD6000u, 0xDB000u, 0xE5000u, 0xEA000u};
        uint32_t h, ok = 1;
        for (h = 0; h < sizeof HIT / sizeof HIT[0]; h++) {
            stock();
            nor[HIT[h]] = 0x46;                    /* (a sample's header or data over the stock layout) */
            ok &= !st_stock_intact();
        }
        stock();
        memset(nor + 0xC8000, 0xFF, 0x1000);       /* (an erased preset head) */
        ok &= !st_stock_intact();
        check("the stock layout checked: its settings' magic, each preset's head and erased tail", ok);
    }
    stock();
    memcpy(nor + 0xD6000, "FSMP", 4);              /* USR2 loaded by an older build over the stock presets */
    tag_erases = 0;
    st_owner_check();
    check("the marker over our samples (an older build): released at boot, the samples kept", tag_erases == 1 &&
          !st_stock_tagged() && !memcmp(nor + 0xD6000, "FSMP", 4));
    fill();
    tag_erases = 0;
    st_owner_check();
    check("no marker: the marker sector never erased", tag_erases == 0);
    check("nothing written outside FL_DATA's objects, the voice bank and the tag", bad_write == 0);
    printf("%s\n", bad ? "SMK OWNER TEST FAILED" : "smk owner test passed");
    return bad != 0;
}
