/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE EDIT screens rendered on the host (same drawing code as the firmware: gfx.c, ui_slice.c),
 * for the docs. The LCD is a 240 x 240 frame buffer here; each shot is written as a PPM.
 *   build/host/slice_shot LOOP OUT_DIR [PALETTE]
 * LOOP: the user loop of slice_test (LOOP.hdr / .bin), put into USR1. Needs FELUCCA_SLICE=1. */
#include <stdarg.h>
#include <stdint.h>
#if !FELUCCA_SLICE
#error "slice_shot needs -DFELUCCA_SLICE=1"
#endif
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3, as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ---- the LCD: a frame buffer (RGB565) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) {
            uint16_t c = px[j * w + i];
            fb[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));   /* gfx.c stores them swapped */
        }
}
#include "../firmware/src/gfx.c"

/* ---- what ui_slice.c takes from the rest of the UI (ui.c, panel.c, ui_input.c, ui_menu.c) */
#define H_HEAD 20
#define Y_HEAD 0
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static struct {
    uint8_t home, force;
} ui;
static struct {
    uint8_t btn[16];
} panel;
static const page_t *cur_page(void) { return &PAGES[0]; }
static void ui_message(const char *m) { (void)m; }
static int32_t enc_in[NE];                               /* the next knob turns */
static int32_t panel_enc(uint32_t k)
{
    int32_t d = enc_in[k];
    enc_in[k] = 0;
    return d;
}
static void enc_drop(void) { memset(enc_in, 0, sizeof enc_in); }
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    (void)role;
    (void)range;
    return s;
}
#include "../firmware/src/ui_slice.c"
static void slc_store_save(void) {}                     /* (project.c: no flash here) */

static void shot(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    ui.force = 1;
    draw_slice_edit();
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc(((c >> 5) & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
    printf("slice_shot: %s\n", path);
}

/* one turn of a knob, as ui_input would hand it over */
static void turn(uint32_t knob, int32_t d)
{
    enc_in[knob] = d;
    slice_edit_input(0);
}

int main(int argc, char **argv)
{
    char path[512];
    const char *loop = argc > 1 ? argv[1] : "build/host/slice_loop", *dir = argc > 2 ? argv[2] : "build/slice_shot";
    long n = -1;
    FILE *f;
    palette_set(argc > 3 ? (uint32_t)atoi(argv[3]) : 0u);
    snprintf(path, sizeof path, "%s.hdr", loop);
    if ((f = fopen(path, "rb"))) {
        n = (long)fread(host_slots, 1, SMP_USER_DATA, f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s.bin", loop);
    if (n == (long)sizeof(smp_user_hdr_t) && (f = fopen(path, "rb"))) {
        n = (long)fread((uint8_t *)host_slots + SMP_USER_DATA, 1, SMP_USER_SIZE - SMP_USER_DATA, f);
        fclose(f);
    } else {
        n = -1;
    }
    if (n <= 0) {
        printf("slice_shot: no user loop (%s.hdr / .bin: see run_tests.sh)\n", loop);
        return 1;
    }
    smp_user_scan(0);
    host_tracks_init();
    host_preset(&trk[0], NENGINES - 1u, 3);              /* USR SLICE: USR1, AUTO */
    song.sel = 0;
    panel.btn[B_PLAY] = B_PLAY;
    panel.btn[B_OCTUP] = B_OCTUP;
    panel.btn[B_OCTDN] = B_OCTDN;

    slice_edit_open();                                   /* the AUTO slices, slice 1 selected */
    shot(dir, "1_open");
    turn(EN_K1, 5);                                      /* slice 6 */
    shot(dir, "2_select");
    turn(EN_K4, 2);                                      /* x4, around its start */
    turn(EN_K2, -4);                                     /* the start a little earlier */
    shot(dir, "3_zoom_start");
    turn(EN_K3, 30);                                     /* KNOB 3: the end; the view follows it */
    shot(dir, "4_zoom_end");
    turn(EN_K4, -2);
    turn(EN_K1, -5);                                     /* slice 1, the longest: split it */
    slice_edit_input(1u << B_OCTUP);
    shot(dir, "5_split");
    turn(EN_K1, 100);                                    /* the last slice: trim the tail */
    turn(EN_K3, -3);
    turn(EN_K4, 2);                                      /* x4 around the end: the cut tail is dim */
    shot(dir, "6_tail_trim");
    return 0;
}
