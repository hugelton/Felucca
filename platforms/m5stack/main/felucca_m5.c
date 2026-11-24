/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca on the M5Stack CoreS3 (MVP): the firmware's sources as the browser build has them (web/emu/
 * felucca_web.c: tests/hostsim.c, then the UI, storage, main.c), against a simulated FM-1 whose screen and audio are
 * the CoreS3's (cores3_hw.c):
 *   - the device clock: fm1_ms and TIMER4 advance with the audio rendered; the audio ISR (audio.c fm1_alnk0_irq)
 *     renders each half, which goes to the I2S DMA (blocking while it is full: that paces everything); the main
 *     loop (main.c's) every 15 ms, ui_input every millisecond in between, all in one task on core 1;
 *   - the screen: the 240 x 240 UI in the middle of the 320 x 240 panel;
 *   - the flash: a 1 MiB NOR image in PSRAM (nothing kept over a power cycle);
 *   - no input: at power-on a demo pattern is written into the tracks and PLAY pressed.
 * Not here: keys, knobs, USB, TRS MIDI, the console, the second core. Nothing of the firmware changes. */
#include <stdint.h>
#include "cores3_hw.h"

#define FELUCCA_WEB 1                     /* (the browser build's paths: no hardware behind them) */
#define FELUCCA_CDC 1
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "M5"
#endif

#define NOR_SIZE 0x100000u
static uint8_t *nor;                      /* the SPI NOR (erased: 0xFF), in PSRAM: m5_main */
#define SMP_USER_XIP(k) ((const uint8_t *)nor + SMP_USER_BASE + (k) * SMP_USER_SIZE)   /* eng_sample.c */
#define main hostsim_main
#include "tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the clock --- */
#define FM1_TICKS_PER_US 24u             /* TIMER4: 24 MHz */
static uint32_t m5_ticks;
static uint32_t fm1_ticks(void) { return m5_ticks; }

/* ------------------------------------------------------ audio (ALNK0) --- */
static uint32_t m5_half;                 /* the half the next render fills */
#define FM1_AUDIO_HALF 1u
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return m5_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t words, void (*isr)(void), uint32_t prio)
{ (void)buf; (void)words; (void)isr; (void)prio; }
static void fm1_audio_stop(void) {}
void isr_alnk0(void) {}
#include "firmware/src/audio.c"

/* ------------------------------------------------------------ the panel --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as bsp/boards/fm1/fm1_input.h */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static void fm1_led_dim_level(uint32_t lo) { (void)lo; }
#include "firmware/bsp/boards/fm1/fm1_led_anim.h"
static void fm1_led_anim_start(uint32_t glow) { (void)glow; }
static int fm1_led_anim_on(void) { return 0; }
static void fm1_led_idle_start(uint32_t glow) { (void)glow; }
static int fm1_led_idle_on(void) { return 0; }
static void fm1_led_anim_stop(void) {}
static uint32_t fm1_input_edges(uint32_t *released)
{
    if (released)
        *released = 0;
    return 0;
}
static uint32_t fm1_input_note_edges(void) { return 0; }
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_wdt_feed(void) {}
static void fm1_irq_off(void) { hw_lock(); }      /* the main loop's "audio ISR off": the audio task waits */
static void fm1_irq_on(void) { hw_unlock(); }

/* ----------------------------------------------------------- the screen --- */
#define LCD_X0 ((HW_LCD_W - 240u) / 2u)  /* the 240 x 240 UI in the middle of the panel */
static uint32_t m5_draw_us;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    if (!w || !h || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;
    if (y + h > 240u)
        h = 240u - y;
    hw_lcd_window(LCD_X0 + x, y, LCD_X0 + x + w - 1u, y + h - 1u);
    hw_lcd_fill(w * h, c);
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    if (!w || !h)
        return;
    hw_lcd_window(LCD_X0 + x, y, LCD_X0 + x + w - 1u, y + h - 1u);
    hw_lcd_pixels(p, w * h * 2u);
}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s)
{
    if (s == 1u)
        hw_lcd_cmd(0x11);
    else if (s == 2u)
        hw_lcd_cmd(0x29);
    else {
        hw_lcd_cmd(0x28);
        hw_lcd_cmd(0x10);
    }
}
static void lcd_wake_now(void) {}
static void lcd_init(void) {}

#include "firmware/src/gfx.c"
#include "firmware/src/panel.c"
#include "firmware/src/ui.c"
#include "firmware/src/menu_items.c"
#include "firmware/src/icons.c"
#include "firmware/src/ui_graph.c"
#include "firmware/src/ui_draw.c"
#include "firmware/src/ui_menu.c"
#include "firmware/src/ui_input.c"
#include "firmware/src/ui_layer.c"

/* ------------------------------------------------------------ the flash --- */
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= NOR_SIZE || n > NOR_SIZE - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off >= NOR_SIZE || 4096u > NOR_SIZE - off)
        return -8;
    memset(nor + (off & ~4095u), 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)       /* NOR: a write only clears bits */
{
    const uint8_t *s = src;
    uint32_t i;
    if (off >= NOR_SIZE || n > NOR_SIZE - off)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { hw_lock(); return 0; }
static void irq_restore(uint32_t f) { (void)f; hw_unlock(); }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }             /* the FM-1's part (project.c) */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "firmware/src/storage.c"
#include "firmware/src/upreset.c"
#include "firmware/src/project.c"

/* ------------------------------------------------- main.c and its HAL --- */
#if FELUCCA_CPU2
typedef struct { uint32_t magic, count, vec, pc, emu, dbg, rets, core; } fm1_crash_t;
#else
typedef struct { uint32_t magic, count, vec, pc, emu, dbg, rets; } fm1_crash_t;
#endif
#define FM1_CRASH_MAGIC 0x43525348u
static fm1_crash_t fm1_crash;
static struct { uint8_t p3_rst, wdt_con; uint32_t rst_src; } fm1_boot;
uint32_t _data_start[1], _data_end[1], _data_load[1], _bss_start[1], _bss_end[1];
uint32_t _pool_start[1], _pool_end[1], _rt_start[1], _rt_end[1], _rt_load[1];
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t m5_master = 576;          /* MASTER, 0..1023 as the pot reads it: the side buttons (m5_touch) */
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? m5_master : 620; }
static void fm1_adc_init(void) {}
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), uint32_t prio) { (void)isr; (void)prio; }
void isr_timer5(void) {}
static void fm1_reboot(void) {}
static void fm1_enter_uboot(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_irq_enable_all(void) {}
static void fm1_irq_init(void) {}
static void fm1_time_init(void) {}
static void fm1_reset_reason(void) {}
static void fm1_wdt_arm(uint32_t v) { (void)v; }
static void fm1_mailbox_clear(void) {}
static void fm1_guard_enable(uint32_t m) { (void)m; }
enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };
#if FELUCCA_CPU2
static uint8_t fm1_cpu1_on, fm1_cpu1_gave_up;
static void fm1_cpu1_stop(void) {}
static int fm1_cpu1_start(void) { return -1; }
#endif
#if FELUCCA_CDC
static void cdc_task(void) {}
#endif
#define usb_poll() ((void)0)
#define usb_start() ((void)0)
#define usb_retry(ms) ((void)(ms))
#define usb_detach() ((void)0)
#include "firmware/src/master.c"
#include "firmware/src/main.c"
#undef usb_poll
#undef usb_start
#undef usb_retry
#undef usb_detach

/* ------------------------------------------------------------- the demo --- */
/* no keys here: a bass (T1 ANALOG), chords (T2 FM6) and a beat (T4 DRUM) on 16 steps, then PLAY */
static void m5_demo(void)
{
    static const uint8_t BASS[16] = {36, 0, 36, 48, 0, 36, 46, 0, 34, 0, 34, 46, 0, 34, 41, 43};
    static const uint8_t CH[2][3] = {{60, 63, 67}, {58, 62, 65}};
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (BASS[i])
            put_step(&trk[0], i, 1, &BASS[i], ST_NOTE, 0);
    put_step(&trk[1], 0, 3, CH[0], ST_NOTE, 0);
    put_step(&trk[1], 8, 3, CH[1], ST_NOTE, 0);
    for (i = 0; i < 16u; i++) {
        uint8_t n[3];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        n[k++] = i % 2u ? 42 : 46;
        put_step(&trk[3], i, k, n, ST_NOTE, 0);
    }
    seq_start();
    ui.force = 1;
}

/* --------------------------------------------------------- the device --- */
static uint32_t m5_boot_ms, m5_last_frame;
static uint32_t m5_dsp_us, m5_ui_us, m5_stat_ms, m5_samples_due;

static void m5_power_on(void)                      /* main.c fm1_main's boot, as felucca_web.c's */
{
    persist_boot();
    settings_init();
    usb_serial_apply();
    lcd_init();
    draw_splash();
    memset(&felucca_dbg, 0, sizeof felucca_dbg);
    felucca_dbg.magic = DBG_MAGIC;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();
    master_boot();
    kb_boot_hold = 1;
    audio_init();
    boot_leds();
    autosave_boot(boot_clean);
    m5_boot_ms = fm1_ms + 430u;
}

static void m5_frame(void)
{
    master_poll();
    felucca_dbg.ui_frames++;
    ui_input();
    settings_poll();
    autosave_poll();
    ui_leds();
    ui_draw();
}

/* the audio task (core 1): the audio ISR's renders, one I2S half at a time, and the device clock from the audio
 * rendered; the lock is the FM-1's "an ISR runs whole between two instructions". The speaker's I2S runs at
 * SPK_FS (a whole divider of 160 MHz: at 44.1 kHz's fractional one the AW88298's PLL never locks), so each render
 * is resampled to it, linearly; the DMA's room paces the renders to FS on the S3's clock. USB gets FS itself. */
#define SPK_FS 44642u                                    /* 160 MHz / 256 / 14 */
#define SPK_STEP ((uint32_t)(((uint64_t)FS << 16) / SPK_FS))   /* input frames per output frame, 16.16 */
static volatile uint32_t m5_booted, m5_peak;
static uint64_t m5_frames;
static int16_t m5_usb[HALF_FRAMES * 2];
static int16_t m5_spk[(HALF_FRAMES + 8) * 2];
static void m5_audio(void)
{
    int32_t prev = 0;                                    /* the last mono sample of the last render */
    uint32_t pos = 0;                                    /* 16.16, from prev (0) through the render (1..128) */
    while (!m5_booted)
        hw_delay_ms(5);
    for (;;) {
        int32_t mono[HALF_FRAMES + 1];
        uint32_t k, t, n = 0;
        const int32_t *o = &abuf[m5_half * HALF_WORDS];
        hw_lock();
        t = hw_us();
        fm1_alnk0_irq();
        m5_dsp_us += hw_us() - t;
        m5_frames += HALF_FRAMES;
        fm1_ms = (uint32_t)(m5_frames * 1000u / FS);
        m5_ticks = fm1_ms * 1000u * FM1_TICKS_PER_US;
        hw_unlock();
        mono[0] = prev;
        for (k = 0; k < HALF_FRAMES; k++) {             /* 24-bit stereo -> 16-bit; the speaker is mono */
            int32_t l = o[2u * k] >> 8, r = o[2u * k + 1u] >> 8;
            l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
            r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
            m5_usb[2u * k] = (int16_t)l;
            m5_usb[2u * k + 1u] = (int16_t)r;
            mono[k + 1u] = (l + r) >> 1;
            if ((uint32_t)(mono[k + 1u] < 0 ? -mono[k + 1u] : mono[k + 1u]) > m5_peak)
                m5_peak = (uint32_t)(mono[k + 1u] < 0 ? -mono[k + 1u] : mono[k + 1u]);
        }
        hw_usb_push(m5_usb, HALF_FRAMES);
        for (; pos < (uint32_t)HALF_FRAMES << 16; pos += SPK_STEP, n++) {
            uint32_t i = pos >> 16, f = pos & 0xFFFFu;
            int32_t v = mono[i] + ((mono[i + 1u] - mono[i]) * (int32_t)f >> 16);
            m5_spk[2u * n] = m5_spk[2u * n + 1u] = (int16_t)v;
        }
        pos -= (uint32_t)HALF_FRAMES << 16;
        prev = mono[HALF_FRAMES];
        hw_audio_write(m5_spk, n);
        m5_half ^= 1u;
    }
}

/* the panel's sides, left and right of the UI, on the UI's background: VOL - and VOL + (the MASTER pot); a tap on the
 * UI: PLAY / STOP */
#define SIDE_FG 0x7BEFu
static uint16_t m5_side_bg = 1;                           /* (1: not drawn yet) */
static void m5_side_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    hw_lcd_window(x, y, x + w - 1u, y + h - 1u);
    hw_lcd_fill(w * h, c);
}
static void m5_sides(void)                                /* again whenever the palette's background changes */
{
    if (m5_side_bg == T_BG)
        return;
    m5_side_bg = T_BG;
    m5_side_rect(0, 0, LCD_X0, HW_LCD_H, T_BG);
    m5_side_rect(LCD_X0 + 240u, 0, HW_LCD_W - LCD_X0 - 240u, HW_LCD_H, T_BG);
    m5_side_rect(12, 119, 16, 2, SIDE_FG);                         /* - */
    m5_side_rect(HW_LCD_W - 28u, 119, 16, 2, SIDE_FG);             /* + */
    m5_side_rect(HW_LCD_W - 21u, 112, 2, 16, SIDE_FG);
}
static uint32_t m5_touch_was;
static void m5_touch(void)
{
    uint32_t x, y;
    int n = hw_touch(&x, &y);
    if (n && !m5_touch_was) {                                     /* a tap: its first frame only */
        if (x < LCD_X0)
            m5_master = m5_master >= 64 ? m5_master - 64 : 0;
        else if (x >= LCD_X0 + 240u)
            m5_master = m5_master <= 1023 - 64 ? m5_master + 64 : 1023;
        else
            transport_req = song.playing ? 2u : 1u;               /* the audio side starts / stops (seq.c) */
        printf("felucca: tap %lu,%lu master %ld\n", (unsigned long)x, (unsigned long)y, (long)m5_master);
    }
    m5_touch_was = n > 0;
}

/* the main loop (core 0): main.c's, a frame every 15 ms and ui_input every millisecond in between */
static void m5_ui(void)
{
    uint32_t last = 0;
    m5_power_on();
    m5_booted = 1;
    for (;;) {
        uint32_t now = fm1_ms, t;
        if (now == last) {
            hw_delay_ms(1);
            continue;
        }
        last = now;
        if ((int32_t)(now - m5_boot_ms) < 0)
            continue;
        if (m5_boot_ms) {                                 /* main.c: the splash goes, the loop starts */
            m5_boot_ms = 0;
            kb_boot_hold = 0;
            lcd_fill(0, 0, 240, 240, T_BG);
            m5_sides();
            m5_last_frame = now - 15u;
            hw_lock();
            m5_demo();
            hw_unlock();
        }
        t = hw_us();
        if (now - m5_last_frame >= 15u) {
            m5_last_frame = now;
            m5_touch();
            m5_frame();
            m5_sides();
        } else {
            ui_input();
        }
        m5_ui_us += hw_us() - t;
        if (now - m5_stat_ms >= 2000u) {                  /* the load and the level, on the serial log */
            printf("felucca: dsp %lu %% (core 1) ui %lu %% (core 0) peak %lu amp %08lx playing %u\n",
                   (unsigned long)(m5_dsp_us / 20000u), (unsigned long)(m5_ui_us / 20000u),
                   (unsigned long)m5_peak, (unsigned long)hw_amp_status(), (unsigned)song.playing);
            m5_dsp_us = m5_ui_us = m5_peak = 0;
            m5_stat_ms = now;
        }
    }
}

#undef __attribute__
extern void *heap_caps_malloc(unsigned int size, uint32_t caps);
#define MALLOC_CAP_SPIRAM (1u << 10)

void app_main(void)
{
    nor = heap_caps_malloc(NOR_SIZE, MALLOC_CAP_SPIRAM);
    memset(nor, 0xFF, NOR_SIZE);
    hw_init();
    hw_audio_init(SPK_FS);
    hw_usb_init();
    hw_start(m5_ui, m5_audio);
}
