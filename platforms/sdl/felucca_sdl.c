/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca on a desktop (Linux, macOS) with SDL2: platforms/host/felucca_host.c (the firmware's sources against a simulated
 * FM-1, as the browser runs them; its API felucca_host.h) with SDL2 for the window, the sound, the keyboard, the mouse
 * and a gamepad, and platforms/common for the panel, the keyboard and gamepad map, the flash file and MIDI's bytes.
 *   - the window: its size given at start (--size W H, --fullscreen), the 240 x 240 screen and the FM-1's panel beside
 *     it (platforms/common/panel.c: WIDE or TALL, the one that shows larger; --layout NONE: the screen alone);
 *   - the sound: 44.1 kHz stereo, queued from the main loop (one thread: the device's state is never shared);
 *   - the keyboard and a gamepad: platforms/common/input.c (the keys by their position, as on a US keyboard);
 *   - the mouse (and touch): the panel's controls (panel.c);
 *   - MIDI: platforms/sdl/midi.c (ALSA, CoreMIDI), channel messages into the device as USB MIDI;
 *   - the flash: the 1 MiB NOR image in a file (default felucca_flash.bin), read at start, written after changes.
 *   platforms/sdl/build.sh  ->  build/sdl/felucca
 *   build/sdl/felucca [flash.bin] [--size W H] [--fullscreen] [--layout wide|tall|none] [--no-midi]
 *                       [--headless MS] [--panel-check] [--midi-check MS] [--shot F.bmp [--marks]]
 *                       [--plugins DIR] [--engines-check] [--plugins-check] [--project-check]   (the checks: sdl_headless ... below) */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../host/felucca_host.c"
#include "../common/panel.c"
#include "../common/input.c"
#include "../common/flashfile.c"
#include "../common/midi_parse.c"
#include "../common/plugins.c"
#include "midi.c"

static panel_ui_t pnl;

static inp_state_t sdl_in;
static flashfile_t sdl_flash;
static const char *sdl_flash_path = "felucca_flash.bin";
#define sent_key (sdl_in.sent_key)

/* ------------------------------------------------------------- the keyboard --- */
/* a key's position (the scancode, as on a US keyboard) as platforms/common/input.c names it; 0 = none of its keys */
static uint32_t sdl_code(SDL_Scancode sc)
{
    static const struct { SDL_Scancode sc; uint16_t code; } K[] = {
        {SDL_SCANCODE_COMMA, ','}, {SDL_SCANCODE_PERIOD, '.'}, {SDL_SCANCODE_SLASH, '/'}, {SDL_SCANCODE_SEMICOLON, ';'},
        {SDL_SCANCODE_APOSTROPHE, '\''}, {SDL_SCANCODE_MINUS, '-'}, {SDL_SCANCODE_EQUALS, '='},
        {SDL_SCANCODE_LEFTBRACKET, '['}, {SDL_SCANCODE_RIGHTBRACKET, ']'}, {SDL_SCANCODE_SPACE, INP_K_SPACE},
        {SDL_SCANCODE_UP, INP_K_UP}, {SDL_SCANCODE_DOWN, INP_K_DOWN}, {SDL_SCANCODE_LEFT, INP_K_LEFT},
        {SDL_SCANCODE_RIGHT, INP_K_RIGHT}, {SDL_SCANCODE_TAB, INP_K_TAB}, {SDL_SCANCODE_ESCAPE, INP_K_ESCAPE},
        {SDL_SCANCODE_RETURN, INP_K_RETURN}, {SDL_SCANCODE_PAGEUP, INP_K_PAGEUP}, {SDL_SCANCODE_PAGEDOWN, INP_K_PAGEDOWN},
    };
    uint32_t i;
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return 'a' + (uint32_t)(sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return '1' + (uint32_t)(sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0)
        return '0';
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
        return INP_K_F1 + (uint32_t)(sc - SDL_SCANCODE_F1);
    for (i = 0; i < sizeof K / sizeof K[0]; i++)
        if (K[i].sc == sc)
            return K[i].code;
    return 0;
}

/* ------------------------------------------------------------- the gamepad --- */
static SDL_GameController *sdl_pad;

static void sdl_pad_event(int button, int down)
{
    static const int8_t PAD[SDL_CONTROLLER_BUTTON_MAX] = {
        [SDL_CONTROLLER_BUTTON_A] = INP_PAD_SOUTH + 1, [SDL_CONTROLLER_BUTTON_X] = INP_PAD_WEST + 1,
        [SDL_CONTROLLER_BUTTON_B] = INP_PAD_EAST + 1, [SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = INP_PAD_L + 1,
        [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = INP_PAD_R + 1, [SDL_CONTROLLER_BUTTON_START] = INP_PAD_START + 1,
        [SDL_CONTROLLER_BUTTON_BACK] = INP_PAD_SELECT + 1, [SDL_CONTROLLER_BUTTON_DPAD_UP] = INP_PAD_UP + 1,
        [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = INP_PAD_DOWN + 1, [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = INP_PAD_LEFT + 1,
        [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = INP_PAD_RIGHT + 1,
    };
    if (button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX && PAD[button])
        inp_pad(&sdl_in, (uint32_t)(PAD[button] - 1), down);
}
static void sdl_pad_stick(void)                    /* every frame: the left stick */
{
    if (sdl_pad)
        inp_stick(&sdl_in, SDL_GameControllerGetAxis(sdl_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f,
                 SDL_GameControllerGetAxis(sdl_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f);
}

/* -------------------------------------------------------------- the flash --- */
static void sdl_flash_load(void)
{
    long n = flashfile_load(&sdl_flash, sdl_flash_path);
    if (n >= 0)
        fprintf(stderr, "felucca: %s, %ld bytes\n", sdl_flash_path, n);
}
#define sdl_flash_save() flashfile_save(&sdl_flash)
#define sdl_sync() inp_sync(&sdl_in)

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
    fh_buttons(1u << label);
    fh_render(441);
    fh_render(441);
    fh_buttons(0);
    fh_render(441);
    fh_render(441);
}

/* the flash file (when one is given) read first; boot, wait for the splash (user preset 1 there or not), the heavy song
 * (fh_test_heavy), PLAY, MS ms of sound, STOP, then SAVE OCT+ OCT+ (user preset 1, as emu_test.mjs) and the flash
 * file written: the level, the screen and the preset, for a check without a window (a second run on the same file:
 * the preset there at boot) */
static int sdl_headless(uint32_t ms, int file)
{
    double sum = 0;
    uint32_t n = 0, k, lit = 0, at_boot, w0;
    uint64_t hash = 14695981039346656037ull;       /* FNV-1a of the samples: the same with an engine's plugin */
    if (file)
        sdl_flash_load();
    else
        fh_nor_erase();
    fh_boot();
    for (k = 0; k < 2000u; k += 10u)
        fh_render(441);
    at_boot = fh_test_user_preset(0);
    fh_test_heavy();                              /* (emu_test.mjs: the heavy song) */
    sdl_tap(B_PLAY);
    for (k = 0; k < ms; k += 10u) {
        uint32_t i;
        fh_render(441);
        for (i = 0; i < 441u; i++, n++)
            sum += (double)out_l[i] * out_l[i] + (double)out_r[i] * out_r[i];
        for (i = 0; i < 441u; i++) {
            uint32_t b[2];
            memcpy(&b[0], &out_l[i], 4);
            memcpy(&b[1], &out_r[i], 4);
            hash = (hash ^ b[0]) * 1099511628211ull;
            hash = (hash ^ b[1]) * 1099511628211ull;
        }
    }
    for (k = 0; k < 240u * 240u; k++)
        lit += fh_screen()[k] != fh_screen()[0];
    printf("felucca headless: %u ms, playing %u, rms %.4f, screen %u px off the background, %u draws\n",
           ms, fh_playing(), n ? sqrt(sum / (2.0 * n)) : 0.0, lit, fh_screen_draws());
    printf("felucca headless: audio %016llx\n", (unsigned long long)hash);
    if (!fh_playing() || sum <= 0 || !lit)
        return 1;
    sdl_tap(B_PLAY);
    w0 = fh_flash_writes();
    sdl_tap(B_SAVE);
    sdl_tap(B_OCTUP);
    sdl_tap(B_OCTUP);
    for (k = 0; k < 200u; k += 10u)
        fh_render(441);
    printf("felucca headless: user preset 1 at boot %u, after SAVE OCT+ OCT+ %u (%u flash writes)\n", at_boot,
           fh_test_user_preset(0), fh_flash_writes() - w0);
    if (file)
        sdl_flash_save();
    return !fh_playing() && fh_test_user_preset(0) == 1u && (at_boot || fh_flash_writes() > w0) ? 0 : 1;
}

/* the engines a plugin can play as (platforms/common/plugins.c), each preset in the heavy song, a second each: a hash a
 * line, the same with an engine's plugin as with the engine built in */
static int sdl_engines_check(void)
{
    static const uint8_t ENG[] = {0, 2, 3, 6, 7, 11};
    uint32_t j, p, k, i, n;
    fh_nor_erase();
    fh_boot();
    for (k = 0; k < 2000u; k += 10u)
        fh_render(441);
    sdl_tap(B_PLAY);
    for (j = 0; j < sizeof ENG; j++) {
        uint64_t hash = 14695981039346656037ull;
        for (p = 0, n = 1; p < n; p += 3u) {
            n = fh_test_engine(ENG[j], p);
            for (k = 0; k < 1000u; k += 10u) {
                fh_render(441);
                for (i = 0; i < 441u; i++) {
                    uint32_t b[2];
                    memcpy(&b[0], &out_l[i], 4);
                    memcpy(&b[1], &out_r[i], 4);
                    hash = (hash ^ b[0]) * 1099511628211ull;
                    hash = (hash ^ b[1]) * 1099511628211ull;
                }
            }
        }
        printf("engine %u %s: %u presets, audio %016llx\n", ENG[j], ENGINES[ENG[j]]->name, n, (unsigned long long)hash);
    }
    return fh_playing() ? 0 : 1;
}

/* the plugins of their own (FE_NEW): the engines listed after Felucca's, each playing (rms), and a number no plugin plays
 * (the last of the range): MISSING, a sine, NO PLUGIN */
static int sdl_plugins_check(void)
{
    uint32_t n, k, i, e, bad = 0;
    fh_nor_erase();
    fh_boot();
    for (k = 0; k < 2000u; k += 10u)
        fh_render(441);
    sdl_tap(B_PLAY);
    printf("plugins: %u engines listed, %u of Felucca's\n", eng_nshown(), (unsigned)NENG_SHOWN);
    if (eng_nshown() > NENG_SHOWN) {                 /* the ENG knob: past Felucca's last, the first plugin, and round */
        e = eng_step(eng_vis(NENG_SHOWN - 1u), 1);
        printf("plugins: after %s, %s; after the last, %s\n", ENGINES[eng_vis(NENG_SHOWN - 1u)]->name, ENGINES[e]->name,
               ENGINES[eng_step(eng_vis(eng_nshown() - 1u), 1)]->name);
        bad |= e != eng_vis(NENG_SHOWN) || eng_step(eng_vis(eng_nshown() - 1u), 1) != eng_vis(0);
    }
    for (n = NENG_SHOWN; n <= eng_nshown(); n++) {
        double sum = 0;
        uint32_t code = 0;
        const char *miss;
        e = n < eng_nshown() ? eng_vis(n) : NENGINES - 1u;
        fh_test_engine(e, 0);
        for (k = 0; k < 1000u; k += 10u) {
            fh_render(441);
            for (i = 0; i < 441u; i++)
                sum += (double)out_l[i] * out_l[i];
        }
        miss = snd_missing(&trk[0], &code);
        printf("engine %u %s (ok %d): rms %.4f, missing %s (code %u)\n", e, ENGINES[e]->name, eng_ok(e),
               sqrt(sum / 44100.0), miss ? miss : "-", code);
        bad |= sum <= 0 || (n < eng_nshown() ? !eng_ok(e) || miss != 0 : eng_ok(e) || !miss || code < 128u);
    }
    return bad || !fh_playing();
}

/* projects keep their plugins' ids (project.c proj_plugin_*): T1 on the first plugin of its own, saved; then the same
 * plugin under another number (another device's), loaded: T1 plays it there; no plugin and no number for the id:
 * NO PLUGIN, and saved again the id is kept; the plugin back under a third number: T1 plays it again */
static int sdl_project_check(void)
{
    uint32_t k, e, id, bad = 0, n0;
    const fe_plugin_t *d;
    fh_nor_erase();
    fh_boot();
    for (k = 0; k < 2000u; k += 10u)
        fh_render(441);
    if (eng_nshown() <= NENG_SHOWN) {
        printf("project: no plugin of its own loaded\n");
        return 1;
    }
    n0 = eng_vis(NENG_SHOWN);
    id = plg_id_of(n0);
    for (k = 0; k < NPLUG && !(plg[k].d && plg[k].no == n0); k++)
        ;
    d = plg[k].d;
    fh_test_engine(n0, 0);
    trk[0].eng_req = (uint8_t)n0;
    bad |= project_save_as(0, "PLUGIN") != 0;
    plg_remove(n0);                                    /* another device: the plugin as number n0 + 5 */
    plg_reg_id = 0, plg_reg_no = 0;
    plg_add(d, n0 + 5u);
    project_load(0);
    e = trk[0].eng_req;
    printf("project: saved with %s as %u (id %08x), loaded where it is %u: T1 %u %s\n", ENGINES[n0 + 5u]->name, n0,
           (unsigned)id, n0 + 5u, e, ENGINES[e]->name);
    bad |= e != n0 + 5u;
    plg_remove(n0 + 5u);                               /* a device without it: NO PLUGIN, the id kept on a save */
    project_load(0);
    e = trk[0].eng_req;
    printf("project: where it is not: T1 %u %s, the id kept %08x\n", e, ENGINES[e]->name, (unsigned)plg_track_id[0]);
    bad |= e != ENGI_UNKNOWN || plg_track_id[0] != id;
    bad |= project_save_as(1, "AGAIN") != 0;
    plg_add(d, n0 + 7u);                               /* installed there later: the project saved there plays it */
    project_load(1);
    e = trk[0].eng_req;
    printf("project: saved there, the plugin installed as %u: T1 %u %s\n", n0 + 7u, e, ENGINES[e]->name);
    bad |= e != n0 + 7u;
    return bad != 0;
}

/* the panel's layouts, without a window (each as a keyboard and as the DRUM grid's pads): every control inside the
 * canvas, clear of the screen and of the others (a black key over its white ones only, on the keyboard), each one
 * found at its own middle, the cursor reaching them all */
static int sdl_panel_check(void)
{
    static const char *const NAME[4] = {"NONE", "WIDE", "TALL", "DUAL"};
    int l, g, bad = 0;
    for (g = 0; g < 6; g++) {                                     /* each layout, as a keyboard and as the DRUM grid */
        int i, j, seen = 0;
        l = PNL_WIDE + g / 2;
        panel_layout(&pnl, l);
        pnl.grid = g & 1;
        pnl_keys_place(&pnl);
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
        printf("felucca panel %s%s: %d x %d, %d controls, %d reach all the others\n", NAME[l], pnl.grid ? " (grid)" : "", pnl.cw,
               pnl.ch, pnl.n, seen);
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
        fh_nor_erase();
        fh_boot();
        for (k = 0; k < 200; k++)
            fh_render(441);
#define CLICK(i, second) (panel_pointer_down(&pnl, pnl.c[i].x + pnl.c[i].w / 2, pnl.c[i].y + pnl.c[i].h / 2, second), \
                          sdl_sync(), fh_render(441), fh_render(441), panel_pointer_up(&pnl), sdl_sync(), fh_render(441))
        fh_test_heavy();
        CLICK(play, 0);
        playing = fh_playing();
        CLICK(play, 0);
        stopped = !fh_playing();
        m0 = fh_master_level();                                     /* MASTER dragged up 30 px: 10 steps */
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
               "let go %u\n", playing, stopped, m0, fh_master_level(), latched, unlatched, stick_key, !((sent_key >> 7) & 1u));
        bad += !playing || !stopped || fh_master_level() != m0 + 160u || !latched || !unlatched || !stick_key || sent_key;
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
    fh_nor_erase();
    fh_boot();
    for (k = 0; k < 2000u; k += 10u)
        fh_render(441);
    fh_test_heavy();
    sdl_tap(B_PLAY);
    for (k = 0; k < ms; k += 10u)
        fh_render(441);
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
    fh_nor_erase();
    fh_boot();
    midi_open();
    for (k = 0; k < ms; k += 10u) {
        SDL_Delay(10);
        midi_poll();
        fh_render(441);
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
    int w = 0, h = 0, full = 0, layout = -1, midi = 1, run = 1, headless_ms = -1, file = 0, marks = 0, mode = 0, i;
    const char *shot = 0, *plugins = "plugins";
    inp_init(&sdl_in, &pnl);
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
        else if (!strcmp(argv[i], "--plugins") && i + 1 < argc)
            plugins = argv[++i];
        else if (!strcmp(argv[i], "--marks"))
            marks = 1;
        else if (!strcmp(argv[i], "--midi-check") && i + 1 < argc)
            return sdl_midi_check((uint32_t)atoi(argv[i + 1]));
        else if (!strcmp(argv[i], "--panel-check"))
            return sdl_panel_check();
        else if (!strcmp(argv[i], "--engines-check"))
            mode = 1;
        else if (!strcmp(argv[i], "--plugins-check"))
            mode = 2;
        else if (!strcmp(argv[i], "--project-check"))
            mode = 3;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: %s [flash.bin] [--size W H] [--fullscreen] [--layout wide|tall|none] [--no-midi]"
                            " [--plugins DIR]\n", argv[0]);
            return 2;
        } else
            sdl_flash_path = argv[i], file = 1;
    }
    {   /* the engine plugins, before the firmware boots; their numbers beside the flash file */
        static char reg[1024];
        const char *sl = strrchr(sdl_flash_path, '/');
        snprintf(reg, sizeof reg, "%.*sfelucca_plugins.txt", sl ? (int)(sl - sdl_flash_path + 1) : 0, sdl_flash_path);
        plugins_load(plugins, reg);
    }
    if (mode)
        return mode == 3 ? sdl_project_check() : mode == 2 ? sdl_plugins_check() : sdl_engines_check();
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
    fh_boot();
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
                    inp_key(&sdl_in, sdl_code(e.key.keysym.scancode), e.type == SDL_KEYDOWN, e.key.repeat);
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
                    sdl_in.key = sdl_in.btn = sdl_in.pad_btn = 0;   /* (as the page on blur: nothing stays held but a latch) */
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
                fh_render(256);
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
            while (fh_now_ms() < now - t0)
                fh_render(441);
        }
        flashfile_poll(&sdl_flash);
        tex = sdl_picture(ren, tex);
    }
    if (fh_flash_writes() != sdl_flash.saved)
        sdl_flash_save();
    midi_close();
    if (dev)
        SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
