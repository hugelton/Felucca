/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca as a libretro core (RetroArch and the other frontends): platforms/host/felucca_host.c (the firmware's sources
 * against a simulated FM-1, as the browser and platforms/sdl run them) and the panel beside the screen
 * (platforms/common/panel.c), with the frontend for the picture, the sound, the input, MIDI and the save
 * (platforms/common: the keyboard and gamepad map, the flash file, MIDI's bytes, the plugin loader).
 *   - no content: the core starts on its own (RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME);
 *   - the picture: the canvas (WIDE 520 x 240, TALL 272 x 500, NONE 240 x 240: the core option felucca_layout) as
 *     XRGB8888, at 60 fps; the frontend scales it;
 *   - the sound: 44.1 kHz stereo, FS / 60 frames a retro_run (the fraction carried);
 *   - the RetroPad as every desktop's gamepad (platforms/common/input.c): the D-pad moves the cursor, the left stick the arrow, B (the bottom
 *     button) holds, Y latches, A lets every latch go, L / R turn, START is PLAY and SELECT HOME;
 *   - the pointer (mouse, touch) on the panel as the window's; the mouse's right button latches, its wheel turns;
 *   - the keyboard as platforms/common/input.c maps it (RetroArch: Game Focus, Scroll Lock, so its hotkeys let the keys through);
 *   - MIDI in through the frontend (RETRO_ENVIRONMENT_GET_MIDI_INTERFACE): channel messages, as platforms/sdl;
 *   - the save: the 1 MiB NOR image in the frontend's save directory, felucca_flash.bin (platforms/sdl's file:
 *     the two share it when copied), read at load, written a second after a change and at unload. (RetroArch skips
 *     the save RAM of a core without content, so not RETRO_MEMORY_SAVE_RAM.)
 *   platforms/libretro/build.sh  ->  build/libretro/felucca_libretro.{so,dylib,dll} */
#include <stdarg.h>
#include "libretro.h"
#include "../host/felucca_host.c"
#include "../common/panel.c"
#include "../common/input.c"
#include "../common/flashfile.c"
#include "../common/midi_parse.c"
#include "../common/plugins.c"

#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "?"
#endif

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;
static retro_log_printf_t log_cb;
static struct retro_midi_interface midi_if;
static int midi_ok;

static panel_ui_t pnl;
static uint32_t canvas[272 * 500];
static inp_state_t lr_in;
static uint32_t frac;                              /* the audio frames' remainder (FS / 60) */
static flashfile_t lr_flash;

static void lr_log(const char *fmt, ...)
{
    char s[256];
    va_list a;
    va_start(a, fmt);
    vsnprintf(s, sizeof s, fmt, a);
    va_end(a);
    if (log_cb)
        log_cb(RETRO_LOG_INFO, "felucca: %s\n", s);
}

#define lr_sync() inp_sync(&lr_in)

/* ------------------------------------------------------------- the keyboard --- */
/* as platforms/sdl/felucca_sdl.c's: the keys F3..G5, the buttons on the number row, the encoders on the arrows and
 * F1..F12; Tab the cursor (the arrows move it, Space holds, Return latches, Page Up / Down turn), Escape lets all go.
 * (The frontend calls this from its own input poll, on the thread that runs retro_run.) */
static uint8_t lr_down[RETROK_LAST];               /* (a key's repeat: down again while it is down) */

/* RETROK_* as platforms/common/input.c names the keys (the printable ones are their characters) */
static uint32_t lr_code(unsigned code)
{
    switch (code) {
    case RETROK_UP: return INP_K_UP;
    case RETROK_DOWN: return INP_K_DOWN;
    case RETROK_LEFT: return INP_K_LEFT;
    case RETROK_RIGHT: return INP_K_RIGHT;
    case RETROK_TAB: return INP_K_TAB;
    case RETROK_ESCAPE: return INP_K_ESCAPE;
    case RETROK_RETURN: return INP_K_RETURN;
    case RETROK_PAGEUP: return INP_K_PAGEUP;
    case RETROK_PAGEDOWN: return INP_K_PAGEDOWN;
    default:
        if (code >= RETROK_F1 && code <= RETROK_F12)
            return INP_K_F1 + (code - RETROK_F1);
        return code >= 32u && code < 127u ? code : 0u;
    }
}

static void RETRO_CALLCONV lr_keyboard(bool down, unsigned code, uint32_t ch, uint16_t mod)
{
    int repeat;
    (void)ch;
    if (code >= RETROK_LAST || (mod & (RETROKMOD_CTRL | RETROKMOD_ALT | RETROKMOD_META)))
        return;
    repeat = down && lr_down[code];
    lr_down[code] = (uint8_t)down;
    inp_key(&lr_in, lr_code(code), down, repeat);
}

/* ------------------------------------------------------------- the RetroPad --- */
static int16_t pad_prev[16];

static int pad_edge(unsigned id, int *down)        /* 1 when the button changed; *down: now */
{
    int16_t v = input_cb(0, RETRO_DEVICE_JOYPAD, 0, id);
    *down = v != 0;
    if ((v != 0) == (pad_prev[id] != 0))
        return 0;
    pad_prev[id] = v;
    return 1;
}

static void lr_pad(void)
{
    static const struct { unsigned id; uint8_t in; } PAD[] = {
        {RETRO_DEVICE_ID_JOYPAD_UP, INP_PAD_UP}, {RETRO_DEVICE_ID_JOYPAD_DOWN, INP_PAD_DOWN},
        {RETRO_DEVICE_ID_JOYPAD_LEFT, INP_PAD_LEFT}, {RETRO_DEVICE_ID_JOYPAD_RIGHT, INP_PAD_RIGHT},
        {RETRO_DEVICE_ID_JOYPAD_B, INP_PAD_SOUTH}, {RETRO_DEVICE_ID_JOYPAD_Y, INP_PAD_WEST},
        {RETRO_DEVICE_ID_JOYPAD_A, INP_PAD_EAST}, {RETRO_DEVICE_ID_JOYPAD_L, INP_PAD_L}, {RETRO_DEVICE_ID_JOYPAD_R, INP_PAD_R},
        {RETRO_DEVICE_ID_JOYPAD_START, INP_PAD_START}, {RETRO_DEVICE_ID_JOYPAD_SELECT, INP_PAD_SELECT},
    };
    uint32_t i;
    int down;
    for (i = 0; i < sizeof PAD / sizeof PAD[0]; i++)    /* (the bottom button is B: SOUTH holds, Y latches, A lets go) */
        if (pad_edge(PAD[i].id, &down))
            inp_pad(&lr_in, PAD[i].in, down);
    inp_stick(&lr_in, input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / 32767.0f,
             input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / 32767.0f);
}

/* ------------------------------------------------------------ the pointer --- */
/* the frontend's pointer (-0x7fff .. 0x7fff over the picture) to the canvas; the mouse's right button and wheel */
static int ptr_down, ptr_x = -1, ptr_y = -1, mouse_right;

static void lr_pointer(void)
{
    int16_t px = input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
    int16_t py = input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
    int pressed = input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
    int right = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0;
    int cx = (int)(((int32_t)px + 0x7fff) * pnl.cw / 0xffff), cy = (int)(((int32_t)py + 0x7fff) * pnl.ch / 0xffff);
    if (input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP))
        panel_wheel(&pnl, 1);
    if (input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
        panel_wheel(&pnl, -1);
    if ((px || py) && (cx != ptr_x || cy != ptr_y)) {   /* (0, 0: no pointer seen yet, as the frontends report it) */
        ptr_x = cx, ptr_y = cy;
        panel_stick_off(&pnl);
        panel_pointer_move(&pnl, cx, cy);
    }
    if (pressed && !ptr_down)
        panel_pointer_down(&pnl, cx, cy, 0);
    else if (!pressed && ptr_down)
        panel_pointer_up(&pnl);
    ptr_down = pressed;
    if (right && !mouse_right && ptr_x >= 0)
        panel_pointer_down(&pnl, ptr_x, ptr_y, 1);       /* (a latch: no release needed) */
    mouse_right = right;
}

/* ------------------------------------------------------------------ MIDI --- */
/* the bytes into channel messages (platforms/common/midi_parse.c: not the clock or SysEx yet) */
static mpar_t lr_mpar;
static void lr_midi(void)
{
    uint8_t b;
    if (!midi_ok || !midi_if.input_enabled())
        return;
    while (midi_if.read(&b))
        mpar_byte(&lr_mpar, b);
}

/* ------------------------------------------------------------ the plugins --- */
/* the engine plugins (platforms/common/plugins.c) in the frontend's system folder, Felucca/plugins (in RetroArch,
 * system/Felucca/plugins), once a process: before the firmware boots; their numbers beside the flash file */
static void lr_plugins(void)
{
    const char *dir = 0, *sav = 0;
    char path[1024];
    static char reg[1024];
    if (fh_booted() || !env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) || !dir || !*dir)
        return;
    if (!env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sav) || !sav || !*sav)
        sav = dir;
    snprintf(path, sizeof path, "%s/Felucca/plugins", dir);
    snprintf(reg, sizeof reg, "%s/felucca_plugins.txt", sav);
    lr_log("%s: %d plugins", path, plugins_load(path, reg));
}

/* -------------------------------------------------------------- the flash --- */
static void lr_flash_load(void)
{
    const char *dir = 0;
    char path[1024];
    long n;
    if (!env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) || !dir || !*dir)
        env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir);
    if (!dir || !*dir) {
        lr_log("no save directory: nothing is kept");
        return;
    }
    if (fh_booted())                               /* (loaded again in the same process: the device runs on) */
        return;
    snprintf(path, sizeof path, "%s/felucca_flash.bin", dir);
    n = flashfile_load(&lr_flash, path);
    if (n >= 0)
        lr_log("%s, %ld bytes", path, n);
    else
        lr_log("%s: a new one", path);
}
#define lr_flash_save() flashfile_save(&lr_flash)
#define lr_flash_poll() flashfile_poll(&lr_flash)

/* ------------------------------------------------------------ the options --- */
static int lr_layout = PNL_WIDE;

static void lr_geometry(struct retro_game_geometry *g)
{
    g->base_width = (unsigned)pnl.cw, g->base_height = (unsigned)pnl.ch;
    g->max_width = 520, g->max_height = 500;
    g->aspect_ratio = (float)pnl.cw / (float)pnl.ch;
}

static void lr_options(int boot)
{
    struct retro_variable v = {"felucca_layout", 0};
    int l = PNL_WIDE;
    if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value)
        l = !strcmp(v.value, "tall") ? PNL_TALL : !strcmp(v.value, "none") ? PNL_NONE : PNL_WIDE;
    if (!boot && l == lr_layout)
        return;
    lr_layout = l;
    panel_layout(&pnl, l);
    if (!boot) {
        struct retro_game_geometry g;
        lr_geometry(&g);
        env_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &g);
    }
}

/* ------------------------------------------------------------- the API --- */
RETRO_API void retro_set_environment(retro_environment_t cb)
{
    static const struct retro_variable VARS[] = {
        {"felucca_layout", "Layout (the panel beside the screen); wide|tall|none"},
        {0, 0},
    };
    static const struct retro_controller_description PORT[] = {{"RetroPad", RETRO_DEVICE_JOYPAD}};
    static const struct retro_controller_info PORTS[] = {{PORT, 1}, {0, 0}};
    bool yes = true;
    env_cb = cb;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &yes);
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)VARS);
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)PORTS);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_cb = cb; }
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof *info);
    info->library_name = "Felucca";
    info->library_version = FELUCCA_VERSION;
    info->valid_extensions = "";
    info->need_fullpath = false;
    info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof *info);
    lr_geometry(&info->geometry);
    info->timing.fps = 60.0;
    info->timing.sample_rate = (double)FS;
}

RETRO_API void retro_init(void)
{
    struct retro_log_callback log;
    if (env_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;
    fh_nor_erase();
    panel_layout(&pnl, PNL_WIDE);
    inp_init(&lr_in, &pnl);
}
RETRO_API void retro_deinit(void) {}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    struct retro_keyboard_callback kb = {lr_keyboard};
    struct retro_midi_interface *mi = &midi_if;
    (void)game;
    if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        lr_log("no XRGB8888");
        return false;
    }
    env_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);
    midi_ok = env_cb(RETRO_ENVIRONMENT_GET_MIDI_INTERFACE, mi) && midi_if.read && midi_if.input_enabled;
    lr_options(1);
    lr_plugins();
    lr_flash_load();
    lr_log("%s, MIDI in %s", FELUCCA_VERSION, midi_ok ? "through the frontend" : "none");
    return true;
}
RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t n)
{
    (void)type, (void)info, (void)n;
    return false;
}
RETRO_API void retro_unload_game(void)
{
    if (fh_flash_writes() != lr_flash.saved)
        lr_flash_save();
}
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

/* the device's state is the firmware's statics: no reset short of a restart, no save states */
RETRO_API void retro_reset(void) {}
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *data, size_t size) { (void)data, (void)size; return false; }
RETRO_API bool retro_unserialize(const void *data, size_t size) { (void)data, (void)size; return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned i, bool on, const char *code) { (void)i, (void)on, (void)code; }

RETRO_API void *retro_get_memory_data(unsigned id) { (void)id; return 0; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port, (void)device; }

RETRO_API void retro_run(void)
{
    static int16_t pcm[2 * 1024];
    bool changed = false;
    uint32_t n, k;
    if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &changed) && changed)
        lr_options(0);
    fh_boot();                                    /* (the first run) */
    poll_cb();
    lr_pad();
    lr_pointer();
    lr_sync();
    lr_midi();
    frac += FS;                                    /* FS / 60 frames, the remainder carried */
    n = frac / 60u;
    frac -= n * 60u;
    while (n) {
        uint32_t m = n > 1024u ? 1024u : n;
        fh_render(m);
        for (k = 0; k < m; k++) {
            float l = out_l[k], r = out_r[k];
            l = l > 1.0f ? 1.0f : l < -1.0f ? -1.0f : l;
            r = r > 1.0f ? 1.0f : r < -1.0f ? -1.0f : r;
            pcm[2u * k] = (int16_t)(l * 32767.0f);
            pcm[2u * k + 1u] = (int16_t)(r * 32767.0f);
        }
        audio_cb(pcm, m);
        n -= m;
    }
    lr_flash_poll();
    panel_render(&pnl, canvas);
    video_cb(canvas, (unsigned)pnl.cw, (unsigned)pnl.ch, (size_t)pnl.cw * sizeof(uint32_t));
}
