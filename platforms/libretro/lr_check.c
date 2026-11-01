/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A check of the libretro core without a frontend: a few lines of one (dlopen, the callbacks), and the core run as
 * RetroArch runs it with no content.
 *   - a second of runs: 44100 audio frames, the picture the layout's size and not one colour;
 *   - a key held from the keyboard callback: sound; the RetroPad's START: PLAY (the green LED in the panel);
 *   - SAVE OCT+ OCT+ (user preset 1) on the keyboard: felucca_flash.bin written in the save directory (here: the
 *     current one) at unload; a second load (--again, another process: the core's state is its statics) reads it,
 *     and its boot leaves it as it was;
 *   - the layout option changed to TALL: the geometry 272 x 500.
 *   cc -o lr_check platforms/libretro/lr_check.c -ldl ; lr_check build/libretro/felucca_libretro.dylib [--again] */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

static unsigned vw, vh, frames, start_down, keys_down;
static double sum;
static const uint32_t *pix;
static size_t pitch;
static retro_keyboard_event_t kb;
static const char *layout = "wide";
static int layout_changed;
static struct retro_game_geometry geom;

static bool env(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = !strcmp(v->key, "felucca_layout") ? layout : 0;
        return v->value != 0;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = layout_changed;
        layout_changed = 0;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:   /* (the plugins: ./Felucca/plugins) */
        *(const char **)data = ".";
        return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        kb = ((struct retro_keyboard_callback *)data)->callback;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        geom = *(struct retro_game_geometry *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        return true;
    }
    return false;
}
static void video(const void *d, unsigned w, unsigned h, size_t p) { pix = d, vw = w, vh = h, pitch = p; }
static void sample(int16_t l, int16_t r) { (void)l, (void)r; }
static size_t batch(const int16_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < 2 * n; i++)
        sum += (double)d[i] * d[i];
    frames += (unsigned)n;
    return n;
}
static void poll(void) {}
static int16_t state(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
    (void)idx;
    return port == 0 && dev == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_START && start_down;
}

#define SYM(name) (*(void **)&lr_##name = dlsym(h, "retro_" #name)) ? (void)0 : (void)(printf("no %s\n", #name), exit(1))
static void (*lr_set_environment)(retro_environment_t), (*lr_set_video_refresh)(retro_video_refresh_t),
    (*lr_set_audio_sample)(retro_audio_sample_t), (*lr_set_audio_sample_batch)(retro_audio_sample_batch_t),
    (*lr_set_input_poll)(retro_input_poll_t), (*lr_set_input_state)(retro_input_state_t), (*lr_init)(void),
    (*lr_run)(void), (*lr_get_system_av_info)(struct retro_system_av_info *), (*lr_deinit)(void);
static bool (*lr_load_game)(const struct retro_game_info *);
static void (*lr_unload_game)(void);

static void run(unsigned n)
{
    while (n--)
        lr_run();
}
static void tap(unsigned code)
{
    kb(true, code, 0, 0);
    run(4);
    kb(false, code, 0, 0);
    run(4);
}
static unsigned colours(void)
{
    unsigned x, y, n = 0;
    for (y = 0; y < vh; y++)
        for (x = 0; x < vw; x++)
            n += (pix[y * (pitch / 4) + x] & 0xFFFFFFu) != (pix[0] & 0xFFFFFFu);
    return n;
}

static uint8_t *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc(*n ? *n : 1);
    *n = fread(b, 1, *n, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    void *h;
    struct retro_system_av_info av;
    uint8_t *before = 0, *after;
    size_t n0 = 0, n1 = 0, used = 0, i;
    int bad = 0, again = argc > 2 && !strcmp(argv[2], "--again");
    unsigned off;
    double rms_quiet, rms_key;
    if (argc < 2 || !(h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL))) {
        printf("usage: lr_check core [--again] (%s)\n", dlerror());
        return 2;
    }
    SYM(set_environment), SYM(set_video_refresh), SYM(set_audio_sample);
    SYM(set_audio_sample_batch), SYM(set_input_poll), SYM(set_input_state), SYM(init);
    SYM(run), SYM(get_system_av_info), SYM(deinit), SYM(load_game);
    SYM(unload_game);
    if (again && !(before = read_file("felucca_flash.bin", &n0)))
        return printf("lr_check --again: no felucca_flash.bin\n"), 1;
    if (!again)
        remove("felucca_flash.bin");
    lr_set_environment(env);
    lr_set_video_refresh(video);
    lr_set_audio_sample(sample);
    lr_set_audio_sample_batch(batch);
    lr_set_input_poll(poll);
    lr_set_input_state(state);
    lr_init();
    if (!lr_load_game(0) || !kb)
        return printf("load: failed (keyboard %d)\n", kb != 0), 1;
    lr_get_system_av_info(&av);
    printf("lr_check: %u x %u, %.0f fps, %.0f Hz\n", av.geometry.base_width, av.geometry.base_height, av.timing.fps,
           av.timing.sample_rate);
    run(60);
    printf("lr_check: 60 runs: %u audio frames, picture %u x %u, %u px off the first\n", frames, vw, vh, off = colours());
    bad += frames != 44100u || vw != 520u || vh != 240u || off < 1000u;
    if (again) {
        run(180);
        lr_unload_game();
        after = read_file("felucca_flash.bin", &n1);
        printf("lr_check --again: felucca_flash.bin %zu bytes, after the boot %s\n", n0,
               after && n1 == n0 && !memcmp(before, after, n0) ? "the same" : "CHANGED");
        bad += !after || n1 != n0 || memcmp(before, after, n0) != 0;
        printf("lr_check: %s\n", bad ? "FAILED" : "ok");
        return bad ? 1 : 0;
    }
    run(120);                                       /* (past the splash) */
    sum = 0, frames = 0;
    run(30);
    rms_quiet = sqrt(sum / (2.0 * frames));
    kb(true, RETROK_z, 0, 0);
    sum = 0, frames = 0;
    run(30);
    rms_key = sqrt(sum / (2.0 * frames));
    kb(false, RETROK_z, 0, 0);
    run(60);
    printf("lr_check: rms quiet %.1f, a key held %.1f\n", rms_quiet, rms_key);
    bad += !(rms_key > 50.0 && rms_key > 4.0 * rms_quiet);
    start_down = 1;                                 /* START: PLAY, a beat of it, START again: stop */
    run(4);
    start_down = 0;
    run(60);
    start_down = 1;
    run(4);
    start_down = 0;
    run(10);
    tap(RETROK_8);                                  /* SAVE OCT+ OCT+: user preset 1 */
    tap(RETROK_RIGHTBRACKET);
    tap(RETROK_RIGHTBRACKET);
    run(30);
    layout = "tall", layout_changed = 1;
    run(2);
    printf("lr_check: layout tall: geometry %u x %u, picture %u x %u\n", geom.base_width, geom.base_height, vw, vh);
    bad += geom.base_width != 272u || geom.base_height != 500u || vw != 272u || vh != 500u;
    lr_unload_game();
    lr_deinit();
    after = read_file("felucca_flash.bin", &n1);
    for (i = 0; after && i < n1; i++)
        used += after[i] != 0xFFu;
    printf("lr_check: after SAVE OCT+ OCT+ and unload: felucca_flash.bin %zu bytes, %zu of them written\n", n1, used);
    bad += !after || n1 != 1048576u || used == 0;
    printf("lr_check: %s\n", bad ? "FAILED" : "ok");
    return bad ? 1 : 0;
}
