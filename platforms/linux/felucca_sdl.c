/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca on a desktop (Linux, macOS) with SDL2: web/emu/felucca_web.c (the firmware's sources against a simulated
 * FM-1, as the browser runs them) built natively, as web/emu/native_check.c does, with SDL2 for the window, the sound,
 * the keyboard, the mouse and a gamepad. Nothing of the firmware or of the browser build changes for this file.
 *   - the window: its size given at start (--size W H, --fullscreen), the 240 x 240 screen and the FM-1's panel beside
 *     it (platforms/common/panel.c: WIDE or TALL, the one that shows larger; --layout NONE: the screen alone);
 *   - the sound: 44.1 kHz stereo, queued from the main loop (one thread: the device's state is never shared);
 *   - the keyboard: as the browser's page (web/emu/index.html), plus the knobs it has only for the mouse; Tab: the
 *     cursor (the arrows move it, Space holds, Return latches, Page Up / Down turn, Escape lets all go);
 *   - the mouse (and touch): the panel's controls (panel.c); a gamepad: the cursor (D-pad) or the arrow (stick);
 *   - MIDI: platforms/linux/midi.c (ALSA, CoreMIDI), channel messages into the device as USB MIDI;
 *   - the flash: the 1 MiB NOR image in a file (default felucca_flash.bin), read at start, written after changes.
 *   platforms/linux/build.sh  ->  build/linux/felucca
 *   build/linux/felucca [flash.bin] [--size W H] [--fullscreen] [--layout wide|tall|none] [--no-midi]
 *                       [--headless MS] [--panel-check] [--midi-check MS] [--shot F.bmp [--marks]]   (the checks: sdl_headless ... below) */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../../web/emu/felucca_web.c"
#include "../common/panel.c"
#include "midi.c"

static panel_ui_t pnl;

/* ------------------------------------------------------------- the keyboard --- */
enum { KM_NONE, KM_KEY, KM_BTN, KM_ENC, KM_MASTER };
typedef struct { SDL_Scancode sc; uint8_t kind, id; int8_t step; } sdl_keymap_t;
static const sdl_keymap_t SDL_KEYS[] = {
    /* the keys F3..G5 (key k = k semitones above F3): Z..slash white, S..' black, then B4..G5 on Q..O */
    {SDL_SCANCODE_Z, KM_KEY, 0, 0}, {SDL_SCANCODE_X, KM_KEY, 2, 0}, {SDL_SCANCODE_C, KM_KEY, 4, 0},
    {SDL_SCANCODE_V, KM_KEY, 6, 0}, {SDL_SCANCODE_B, KM_KEY, 7, 0}, {SDL_SCANCODE_N, KM_KEY, 9, 0},
    {SDL_SCANCODE_M, KM_KEY, 11, 0}, {SDL_SCANCODE_COMMA, KM_KEY, 12, 0}, {SDL_SCANCODE_PERIOD, KM_KEY, 14, 0},
    {SDL_SCANCODE_SLASH, KM_KEY, 16, 0},
    {SDL_SCANCODE_S, KM_KEY, 1, 0}, {SDL_SCANCODE_D, KM_KEY, 3, 0}, {SDL_SCANCODE_F, KM_KEY, 5, 0},
    {SDL_SCANCODE_H, KM_KEY, 8, 0}, {SDL_SCANCODE_J, KM_KEY, 10, 0}, {SDL_SCANCODE_L, KM_KEY, 13, 0},
    {SDL_SCANCODE_SEMICOLON, KM_KEY, 15, 0}, {SDL_SCANCODE_APOSTROPHE, KM_KEY, 17, 0},
    {SDL_SCANCODE_Q, KM_KEY, 18, 0}, {SDL_SCANCODE_W, KM_KEY, 19, 0}, {SDL_SCANCODE_E, KM_KEY, 20, 0},
    {SDL_SCANCODE_R, KM_KEY, 21, 0}, {SDL_SCANCODE_T, KM_KEY, 22, 0}, {SDL_SCANCODE_Y, KM_KEY, 23, 0},
    {SDL_SCANCODE_U, KM_KEY, 24, 0}, {SDL_SCANCODE_I, KM_KEY, 25, 0}, {SDL_SCANCODE_O, KM_KEY, 26, 0},
    /* the buttons on the number row in the panel's order (FX SCL ENV LFO EDIT GLO, HOME SAVE ARP SEQ PLAY REC), [ ] OCT */
    {SDL_SCANCODE_1, KM_BTN, B_FX, 0}, {SDL_SCANCODE_2, KM_BTN, B_SCL, 0}, {SDL_SCANCODE_3, KM_BTN, B_ENV, 0},
    {SDL_SCANCODE_4, KM_BTN, B_LFO, 0}, {SDL_SCANCODE_5, KM_BTN, B_EDIT, 0}, {SDL_SCANCODE_6, KM_BTN, B_GLO, 0},
    {SDL_SCANCODE_7, KM_BTN, B_HOME, 0}, {SDL_SCANCODE_8, KM_BTN, B_SAVE, 0}, {SDL_SCANCODE_9, KM_BTN, B_ARP, 0},
    {SDL_SCANCODE_0, KM_BTN, B_SEQ, 0}, {SDL_SCANCODE_MINUS, KM_BTN, B_PLAY, 0}, {SDL_SCANCODE_EQUALS, KM_BTN, B_REC, 0},
    {SDL_SCANCODE_LEFTBRACKET, KM_BTN, B_OCTDN, 0}, {SDL_SCANCODE_RIGHTBRACKET, KM_BTN, B_OCTUP, 0},
    /* the encoders: the arrows as the page (SELECT up / down, ALGORITHM right / left), F1..F10 the others, F11 F12 MASTER */
    {SDL_SCANCODE_UP, KM_ENC, EN_SELECT, 1}, {SDL_SCANCODE_DOWN, KM_ENC, EN_SELECT, -1},
    {SDL_SCANCODE_RIGHT, KM_ENC, EN_ALGO, 1}, {SDL_SCANCODE_LEFT, KM_ENC, EN_ALGO, -1},
    {SDL_SCANCODE_F1, KM_ENC, EN_PRESET, -1}, {SDL_SCANCODE_F2, KM_ENC, EN_PRESET, 1},
    {SDL_SCANCODE_F3, KM_ENC, EN_K1, -1}, {SDL_SCANCODE_F4, KM_ENC, EN_K1, 1},
    {SDL_SCANCODE_F5, KM_ENC, EN_K2, -1}, {SDL_SCANCODE_F6, KM_ENC, EN_K2, 1},
    {SDL_SCANCODE_F7, KM_ENC, EN_K3, -1}, {SDL_SCANCODE_F8, KM_ENC, EN_K3, 1},
    {SDL_SCANCODE_F9, KM_ENC, EN_K4, -1}, {SDL_SCANCODE_F10, KM_ENC, EN_K4, 1},
    {SDL_SCANCODE_F11, KM_MASTER, 0, -1}, {SDL_SCANCODE_F12, KM_MASTER, 0, 1},
};
static uint32_t sdl_btn, sdl_key, sdl_pad_btn, sent_btn = ~0u, sent_key = ~0u;

/* the keyboard's, the panel's and the gamepad's together, to the device when they change */
static void sdl_sync(void)
{
    uint32_t b = sdl_btn | sdl_pad_btn | panel_buttons(&pnl), k = sdl_key | panel_keys(&pnl);
    if (b != sent_btn)
        web_buttons(sent_btn = b);
    if (k != sent_key)
        web_keys(sent_key = k);
}

static void sdl_key_event(SDL_Scancode sc, int down, int repeat)
{
    uint32_t i;
    if (sc == SDL_SCANCODE_TAB) {                  /* the cursor on / off */
        if (down && !repeat)
            panel_cursor_on(&pnl, !pnl.cursor);
        return;
    }
    if (sc == SDL_SCANCODE_ESCAPE) {
        if (down)
            panel_release_all(&pnl);
        return;
    }
    if (pnl.cursor) {
        int dx = sc == SDL_SCANCODE_RIGHT ? 1 : sc == SDL_SCANCODE_LEFT ? -1 : 0;
        int dy = sc == SDL_SCANCODE_DOWN ? 1 : sc == SDL_SCANCODE_UP ? -1 : 0;
        if (dx || dy) {
            if (down)
                panel_cursor_move(&pnl, dx, dy);
            return;
        }
        if (sc == SDL_SCANCODE_SPACE) {
            if (!repeat)
                panel_cursor_press(&pnl, down);
            return;
        }
        if (sc == SDL_SCANCODE_RETURN) {
            if (down && !repeat)
                panel_cursor_latch(&pnl);
            return;
        }
        if (sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN) {
            if (down)
                panel_cursor_turn(&pnl, sc == SDL_SCANCODE_PAGEUP ? 1 : -1);
            return;
        }
    }
    for (i = 0; i < sizeof SDL_KEYS / sizeof SDL_KEYS[0]; i++) {
        const sdl_keymap_t *k = &SDL_KEYS[i];
        if (k->sc != sc)
            continue;
        if (k->kind == KM_KEY && !repeat)
            sdl_key = down ? sdl_key | 1u << k->id : sdl_key & ~(1u << k->id);
        else if (k->kind == KM_BTN && !repeat)
            sdl_btn = down ? sdl_btn | 1u << k->id : sdl_btn & ~(1u << k->id);
        else if (k->kind == KM_ENC && down)
            web_enc(k->id, k->step);
        else if (k->kind == KM_MASTER && down)
            pnl_turn(&pnl, PC_MASTER, 2 * k->step);
        return;
    }
}

/* ------------------------------------------------------------- the gamepad --- */
/* the D-pad moves the cursor (it comes on), the left stick the arrow (panel.c: the one moved last is used); A holds
 * (or clicks the arrow), X latches, B lets all go, the shoulders turn, START is PLAY and BACK HOME (held while they are) */
static SDL_GameController *sdl_pad;

static void sdl_pad_stick(void)                    /* every frame: the left stick, past a dead zone, moves the arrow */
{
    float x, y, m;
    if (!sdl_pad)
        return;
    x = SDL_GameControllerGetAxis(sdl_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    y = SDL_GameControllerGetAxis(sdl_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    m = x * x + y * y;
    if (m < 0.04f)
        return;
    panel_stick_move(&pnl, x * 12.0f * m, y * 12.0f * m);      /* (slow near the middle: aiming) */
}

static void sdl_pad_event(int button, int down)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        if (down) {
            panel_stick_off(&pnl);
            if (!pnl.cursor)
                panel_cursor_on(&pnl, 1);
            else
                panel_cursor_move(&pnl, button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? 1 : button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 0,
                                  button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 1 : button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 0);
        }
        break;
    case SDL_CONTROLLER_BUTTON_A:
        if (pnl.stick)
            panel_stick_click(&pnl, down, 0);
        else {
            if (!pnl.cursor)
                panel_cursor_on(&pnl, 1);
            panel_cursor_press(&pnl, down);
        }
        break;
    case SDL_CONTROLLER_BUTTON_X:
        if (down && pnl.stick)
            panel_stick_click(&pnl, 1, 1);
        else if (down)
            panel_cursor_latch(&pnl);
        break;
    case SDL_CONTROLLER_BUTTON_B:
        if (down)
            panel_release_all(&pnl);
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        if (down)
            panel_cursor_turn(&pnl, button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ? 1 : -1);
        break;
    case SDL_CONTROLLER_BUTTON_START:
        sdl_pad_btn = down ? sdl_pad_btn | 1u << B_PLAY : sdl_pad_btn & ~(1u << B_PLAY);
        break;
    case SDL_CONTROLLER_BUTTON_BACK:
        sdl_pad_btn = down ? sdl_pad_btn | 1u << B_HOME : sdl_pad_btn & ~(1u << B_HOME);
        break;
    }
}

/* -------------------------------------------------------------- the flash --- */
static const char *sdl_flash_path = "felucca_flash.bin";
static uint32_t sdl_flash_saved, sdl_flash_changed_ms;

static void sdl_flash_load(void)
{
    FILE *f = fopen(sdl_flash_path, "rb");
    web_nor_erase();
    if (f) {
        size_t n = fread(web_nor(), 1, web_nor_size(), f);
        fclose(f);
        fprintf(stderr, "felucca: %s, %u bytes\n", sdl_flash_path, (unsigned)n);
    }
    sdl_flash_saved = web_flash_writes_count();
}
static void sdl_flash_save(void)
{
    char tmp[1024];
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s.tmp", sdl_flash_path);
    f = fopen(tmp, "wb");
    if (!f || fwrite(web_nor(), 1, web_nor_size(), f) != web_nor_size() || fclose(f) != 0) {
        fprintf(stderr, "felucca: cannot write %s\n", tmp);
        return;
    }
    if (rename(tmp, sdl_flash_path) != 0)
        fprintf(stderr, "felucca: cannot write %s\n", sdl_flash_path);
    sdl_flash_saved = web_flash_writes_count();
}
static void sdl_flash_poll(void)                       /* a second after the last write */
{
    uint32_t w = web_flash_writes_count();
    if (w == sdl_flash_saved)
        return;
    if (!sdl_flash_changed_ms)
        sdl_flash_changed_ms = web_now_ms();
    if (web_now_ms() - sdl_flash_changed_ms >= 1000u) {
        sdl_flash_save();
        sdl_flash_changed_ms = 0;
    }
}

/* ------------------------------------------------------------- the picture --- */
/* panel.c draws the screen and the panel in the screen's pixels and scales them to the window's own pixels (a
 * Retina display's too); the texture is the window's size, copied 1 : 1 */
static uint32_t sdl_canvas[272 * 500], *sdl_out;
static int sdl_box[4], sdl_ow, sdl_oh;            /* where the picture is in the output, the output's size */
static float sdl_px_per_pt = 1.0f;                /* output pixels per window point (the mouse comes in points) */

static SDL_Texture *sdl_output(SDL_Renderer *ren, SDL_Texture *tex, int w, int h)
{
    if (tex && w == sdl_ow && h == sdl_oh)
        return tex;
    if (tex)
        SDL_DestroyTexture(tex);
    sdl_ow = w, sdl_oh = h;
    sdl_out = realloc(sdl_out, sizeof *sdl_out * (size_t)w * (size_t)h);
    return SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
}
static SDL_Texture *sdl_picture(SDL_Renderer *ren, SDL_Texture *tex)
{
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    tex = sdl_output(ren, tex, w, h);
    panel_render(&pnl, sdl_canvas);
    panel_scale(sdl_canvas, pnl.cw, pnl.ch, sdl_out, w, h, sdl_box);
    SDL_UpdateTexture(tex, 0, sdl_out, w * (int)sizeof(uint32_t));
    SDL_RenderCopy(ren, tex, 0, 0);
    SDL_RenderPresent(ren);
    return tex;
}
static void sdl_to_canvas(int x, int y, int *cx, int *cy)   /* a window point to a canvas pixel */
{
    float px = x * sdl_px_per_pt, py = y * sdl_px_per_pt;
    *cx = sdl_box[2] ? (int)floorf((px - sdl_box[0]) * pnl.cw / sdl_box[2]) : 0;
    *cy = sdl_box[3] ? (int)floorf((py - sdl_box[1]) * pnl.ch / sdl_box[3]) : 0;
}

/* ------------------------------------------------------------ the headless run --- */
static void sdl_tap(uint32_t label)                /* (emu_test.mjs a.tap) */
{
    web_buttons(1u << label);
    web_render(441);
    web_render(441);
    web_buttons(0);
    web_render(441);
    web_render(441);
}

/* the flash file (when one is given) read first; boot, wait for the splash (user preset 1 there or not), the heavy song
 * (web_test_heavy), PLAY, MS ms of sound, STOP, then SAVE OCT+ OCT+ (user preset 1, as emu_test.mjs) and the flash
 * file written: the level, the screen and the preset, for a check without a window (a second run on the same file:
 * the preset there at boot) */
static int sdl_headless(uint32_t ms, int file)
{
    double sum = 0;
    uint32_t n = 0, k, lit = 0, at_boot, w0;
    if (file)
        sdl_flash_load();
    else
        web_nor_erase();
    web_boot();
    for (k = 0; k < 2000u; k += 10u)
        web_render(441);
    at_boot = web_test_user_preset(0);
    web_test_heavy();                              /* (emu_test.mjs: the heavy song) */
    sdl_tap(B_PLAY);
    for (k = 0; k < ms; k += 10u) {
        uint32_t i;
        web_render(441);
        for (i = 0; i < 441u; i++, n++)
            sum += (double)out_l[i] * out_l[i] + (double)out_r[i] * out_r[i];
    }
    for (k = 0; k < 240u * 240u; k++)
        lit += web_fb[k] != web_fb[0];
    printf("felucca headless: %u ms, playing %u, rms %.4f, screen %u px off the background, %u draws\n",
           ms, web_playing(), n ? sqrt(sum / (2.0 * n)) : 0.0, lit, web_screen_draws());
    if (!web_playing() || sum <= 0 || !lit)
        return 1;
    sdl_tap(B_PLAY);
    w0 = web_flash_writes_count();
    sdl_tap(B_SAVE);
    sdl_tap(B_OCTUP);
    sdl_tap(B_OCTUP);
    for (k = 0; k < 200u; k += 10u)
        web_render(441);
    printf("felucca headless: user preset 1 at boot %u, after SAVE OCT+ OCT+ %u (%u flash writes)\n", at_boot,
           web_test_user_preset(0), web_flash_writes_count() - w0);
    if (file)
        sdl_flash_save();
    return !web_playing() && web_test_user_preset(0) == 1u && (at_boot || web_flash_writes_count() > w0) ? 0 : 1;
}

/* the panel's layouts, without a window: every control inside the canvas, clear of the screen and of the others
 * (a black key over its white ones only), each one found at its own middle, the cursor reaching them all */
static int sdl_panel_check(void)
{
    static const char *const NAME[3] = {"NONE", "WIDE", "TALL"};
    int l, bad = 0;
    for (l = PNL_WIDE; l <= PNL_TALL; l++) {
        int i, j, seen = 0;
        panel_layout(&pnl, l);
        for (i = 0; i < pnl.n; i++) {
            const pnl_ctl_t *a = &pnl.c[i];
            int mx = a->x + a->w / 2, my = a->y + (a->kind == PC_KEY ? a->h - 4 : a->h / 2), h;
            if (a->kind == PC_KEY && a->w < 15)
                my = a->y + a->h / 2;
            if (a->x < 0 || a->y < 0 || a->x + a->w > pnl.cw || a->y + a->h + (a->kind == PC_KNOB ? 24 : 0) > pnl.ch)
                printf("  %s: control %d outside the canvas\n", NAME[l], i), bad++;
            if (a->x < pnl.lcd_x + pnl.lcd_s && a->x + a->w > pnl.lcd_x && a->y < pnl.lcd_y + pnl.lcd_s && a->y + a->h > pnl.lcd_y)
                printf("  %s: control %d over the screen\n", NAME[l], i), bad++;
            for (j = i + 1; j < pnl.n; j++) {
                const pnl_ctl_t *b = &pnl.c[j];
                if (a->kind == PC_KEY && b->kind == PC_KEY && (a->w < 15) != (b->w < 15))
                    continue;
                if (a->x < b->x + b->w && a->x + a->w > b->x && a->y < b->y + b->h && a->y + a->h > b->y)
                    printf("  %s: controls %d and %d overlap\n", NAME[l], i, j), bad++;
            }
            h = pnl_hit(&pnl, mx, my);
            if (h != i)
                printf("  %s: control %d (kind %d id %d) not found at its middle (%d)\n", NAME[l], i, a->kind, a->id, h), bad++;
        }
        panel_cursor_on(&pnl, 1);
        for (i = 0; i < pnl.n; i++) {                                 /* every control reached from every other */
            int t, ok = 1;
            for (t = 0; t < pnl.n && ok; t++) {
                int mark[PNL_NCTL] = {0}, queue[PNL_NCTL], qh = 0, qt = 0, d;
                if (t == i)
                    continue;
                queue[qt++] = i, mark[i] = 1;
                while (qh < qt && !mark[t]) {
                    int f = queue[qh++];
                    for (d = 0; d < 4; d++) {
                        static const int D[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                        pnl.focus = f;
                        panel_cursor_move(&pnl, D[d][0], D[d][1]);
                        if (!mark[pnl.focus])
                            mark[pnl.focus] = 1, queue[qt++] = pnl.focus;
                    }
                }
                ok = mark[t];
            }
            if (!ok)
                printf("  %s: not every control reached from control %d\n", NAME[l], i), bad++;
            seen += ok;
        }
        printf("felucca panel %s: %d x %d, %d controls, %d reach all the others\n", NAME[l], pnl.cw, pnl.ch, pnl.n, seen);
        panel_cursor_on(&pnl, 0);
    }
    {                                                            /* playing it: the device behind the panel */
        int k, play = -1, master = -1, key = -1;
        uint32_t m0, playing, stopped, latched, unlatched, stick_key;
        panel_layout(&pnl, PNL_WIDE);
        for (k = 0; k < pnl.n; k++) {
            if (pnl.c[k].kind == PC_BTN && pnl.c[k].id == B_PLAY) play = k;
            if (pnl.c[k].kind == PC_KNOB && pnl.c[k].id == PC_MASTER) master = k;
            if (pnl.c[k].kind == PC_KEY && pnl.c[k].id == 7) key = k;
        }
        web_nor_erase();
        web_boot();
        for (k = 0; k < 200; k++)
            web_render(441);
#define CLICK(i, second) (panel_pointer_down(&pnl, pnl.c[i].x + pnl.c[i].w / 2, pnl.c[i].y + pnl.c[i].h / 2, second), \
                          sdl_sync(), web_render(441), web_render(441), panel_pointer_up(&pnl), sdl_sync(), web_render(441))
        web_test_heavy();
        CLICK(play, 0);
        playing = web_playing();
        CLICK(play, 0);
        stopped = !web_playing();
        m0 = web_master_adc;                                     /* MASTER dragged up 30 px: 10 steps */
        panel_pointer_down(&pnl, pnl.c[master].x + 15, pnl.c[master].y + 36, 0);
        for (k = 1; k <= 30; k++)
            panel_pointer_move(&pnl, pnl.c[master].x + 15, pnl.c[master].y + 36 - k);
        panel_pointer_up(&pnl);
        CLICK(key, 1);                                           /* a key latched (second button), then let go */
        latched = (sent_key >> 7) & 1u;
        CLICK(key, 1);
        unlatched = !((sent_key >> 7) & 1u);
        pnl.sx = 0, pnl.sy = 0;                                  /* the arrow to the key, A down */
        panel_stick_move(&pnl, (float)(pnl.c[key].x + 10), (float)(pnl.c[key].y + pnl.c[key].h - 20));
        panel_stick_click(&pnl, 1, 0);
        sdl_sync();
        stick_key = (sent_key >> 7) & 1u;
        panel_stick_click(&pnl, 0, 0);
        sdl_sync();
        printf("felucca panel: PLAY clicked %u, again %u; MASTER %u -> %u; key latched %u, let go %u; the arrow's click %u, "
               "let go %u\n", playing, stopped, m0, web_master_adc, latched, unlatched, stick_key, !((sent_key >> 7) & 1u));
        bad += !playing || !stopped || web_master_adc != m0 + 160u || !latched || !unlatched || !stick_key || sent_key;
#undef CLICK
    }
    printf("felucca panel: fit 1280x720 %s, 800x1280 %s, 640x480 %s\n", NAME[panel_fit(1280, 720)], NAME[panel_fit(800, 1280)],
           NAME[panel_fit(640, 480)]);
    return bad ? 1 : 0;
}

/* a picture without a window: the heavy song played MS ms (cursor and arrow shown when asked), drawn by the window's
 * own drawing (sdl_picture) into a W x H surface, saved as a BMP */
static int sdl_shot(const char *path, int w, int h, int layout, uint32_t ms, int marks)
{
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *ren = sf ? SDL_CreateSoftwareRenderer(sf) : 0;
    SDL_Texture *tex = 0;
    uint32_t k;
    if (!ren) {
        fprintf(stderr, "felucca: %s\n", SDL_GetError());
        return 1;
    }
    panel_layout(&pnl, layout < 0 ? panel_fit(w, h) : layout);
    web_nor_erase();
    web_boot();
    for (k = 0; k < 2000u; k += 10u)
        web_render(441);
    web_test_heavy();
    sdl_tap(B_PLAY);
    for (k = 0; k < ms; k += 10u)
        web_render(441);
    if (marks) {                                  /* the cursor on PLAY's neighbour, a latch, the arrow on a key */
        int i;
        panel_cursor_on(&pnl, 1);
        for (i = 0; i < pnl.n; i++)
            if (pnl.c[i].kind == PC_BTN && pnl.c[i].id == B_SEQ)
                pnl.focus = i;
        pnl.latch_btns = 1u << B_FX;
        pnl.stick = 1, pnl.sx = (float)(pnl.c[pnl.n - 20].x + 6), pnl.sy = (float)(pnl.c[pnl.n - 20].y + 60);
    }
    tex = sdl_picture(ren, tex);
    if (!tex || SDL_SaveBMP(sf, path) != 0) {
        fprintf(stderr, "felucca: %s\n", SDL_GetError());
        return 1;
    }
    printf("felucca shot: %s, %d x %d, layout %d\n", path, w, h, pnl.layout);
    return 0;
}

/* MIDI in without a window: MS ms of the device listening (midi.c), what came in and what it played */
static int sdl_midi_check(uint32_t ms)
{
    double sum = 0;
    uint32_t k, i;
    web_nor_erase();
    web_boot();
    midi_open();
    for (k = 0; k < ms; k += 10u) {
        SDL_Delay(10);
        midi_poll();
        web_render(441);
        for (i = 0; i < 441u; i++)
            sum += (double)out_l[i] * out_l[i] + (double)out_r[i] * out_r[i];
    }
    printf("felucca midi: %u messages in, rms %.4f\n", midi_count, sqrt(sum / (2.0 * 441.0 * (ms / 10u ? ms / 10u : 1u))));
    midi_close();
    return midi_count && sum > 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ main --- */
int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_AudioDeviceID dev;
    SDL_AudioSpec want, have;
    int w = 0, h = 0, full = 0, layout = -1, midi = 1, run = 1, headless_ms = -1, file = 0, marks = 0, i;
    const char *shot = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--size") && i + 2 < argc)
            w = atoi(argv[i + 1]), h = atoi(argv[i + 2]), i += 2;
        else if (!strcmp(argv[i], "--fullscreen"))
            full = 1;
        else if (!strcmp(argv[i], "--layout") && i + 1 < argc) {
            i++;
            layout = !strcmp(argv[i], "wide") ? PNL_WIDE : !strcmp(argv[i], "tall") ? PNL_TALL : PNL_NONE;
        } else if (!strcmp(argv[i], "--no-midi"))
            midi = 0;
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc)
            headless_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            shot = argv[++i];
        else if (!strcmp(argv[i], "--marks"))
            marks = 1;
        else if (!strcmp(argv[i], "--midi-check") && i + 1 < argc)
            return sdl_midi_check((uint32_t)atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--panel-check"))
            return sdl_panel_check();
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: %s [flash.bin] [--size W H] [--fullscreen] [--layout wide|tall|none] [--no-midi]\n", argv[0]);
            return 2;
        } else
            sdl_flash_path = argv[i], file = 1;
    }
    if (headless_ms >= 0)
        return sdl_headless((uint32_t)headless_ms, file);
    if (shot)
        return sdl_shot(shot, w ? w : 1040, h ? h : 480, layout, 1500, marks);
    sdl_flash_load();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "felucca: %s\n", SDL_GetError());
        return 1;
    }
    if (full && (!w || !h)) {                                     /* fullscreen: the display's size */
        SDL_DisplayMode m;
        if (SDL_GetDesktopDisplayMode(0, &m) == 0)
            w = m.w, h = m.h;
    }
    if (layout < 0)
        layout = w && h ? panel_fit(w, h) : PNL_WIDE;
    panel_layout(&pnl, layout);
    if (!w || !h)                                                 /* no size: the canvas, 2 x (3 x for NONE) */
        w = pnl.cw * (layout == PNL_NONE ? 3 : 2), h = pnl.ch * (layout == PNL_NONE ? 3 : 2);
    win = SDL_CreateWindow("Felucca", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                           (full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) | SDL_WINDOW_ALLOW_HIGHDPI);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : 0;
    tex = 0;
    if (!ren) {
        fprintf(stderr, "felucca: %s\n", SDL_GetError());
        return 1;
    }
    {
        int ow, oh, ww, wh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_GetWindowSize(win, &ww, &wh);
        sdl_px_per_pt = ww ? (float)ow / ww : 1.0f;
    }
    SDL_zero(want);
    want.freq = (int)FS;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    dev = SDL_OpenAudioDevice(0, 0, &want, &have, 0);
    if (!dev)
        fprintf(stderr, "felucca: no sound (%s)\n", SDL_GetError());
    else
        SDL_PauseAudioDevice(dev, 0);
    if (midi)
        midi_open();
    web_boot();
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                run = 0;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (!(e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI | KMOD_ALT)))
                    sdl_key_event(e.key.keysym.scancode, e.type == SDL_KEYDOWN, e.key.repeat);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
                    int cx, cy;
                    sdl_to_canvas(e.button.x, e.button.y, &cx, &cy);
                    panel_pointer_down(&pnl, cx, cy, e.button.button == SDL_BUTTON_RIGHT);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT)
                    panel_pointer_up(&pnl);
                break;
            case SDL_MOUSEMOTION:
                {
                    int cx, cy;
                    sdl_to_canvas(e.motion.x, e.motion.y, &cx, &cy);
                    panel_stick_off(&pnl);
                    panel_pointer_move(&pnl, cx, cy);
                }
                break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.y)
                    panel_wheel(&pnl, e.wheel.y > 0 ? 1 : -1);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!sdl_pad)
                    sdl_pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                sdl_pad_event(e.cbutton.button, e.type == SDL_CONTROLLERBUTTONDOWN);
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    sdl_key = sdl_btn = sdl_pad_btn = 0;   /* (as the page on blur: nothing stays held but a latch) */
                    panel_pointer_up(&pnl);
                    panel_cursor_press(&pnl, 0);
                }
                break;
            }
        }
        sdl_pad_stick();
        sdl_sync();
        if (midi)
            midi_poll();
        /* the device runs as far as the sound asks: about 40 ms queued (without sound: by the clock) */
        if (dev) {
            while (SDL_GetQueuedAudioSize(dev) < 1792u * 2u * sizeof(float)) {
                float buf[256 * 2];
                uint32_t k;
                web_render(256);
                for (k = 0; k < 256u; k++) {
                    buf[2u * k] = out_l[k];
                    buf[2u * k + 1u] = out_r[k];
                }
                SDL_QueueAudio(dev, buf, sizeof buf);
            }
        } else {
            static uint32_t t0;
            uint32_t now = SDL_GetTicks();
            if (!t0)
                t0 = now;
            while (web_now_ms() < now - t0)
                web_render(441);
        }
        sdl_flash_poll();
        tex = sdl_picture(ren, tex);
    }
    if (web_flash_writes_count() != sdl_flash_saved)
        sdl_flash_save();
    midi_close();
    if (dev)
        SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
