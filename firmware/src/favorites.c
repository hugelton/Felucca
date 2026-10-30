/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define FELUCCA_FAVORITES 1
/* Stable engine/preset references; fixed capacity independent of optional engines. Engines 0..13 have a row of
 * favourites each (a bit per factory preset); rows 14 and 15 hold the MIDI LEARN map (midi_learn.c) and settings
 * (settings_persist.c), so an engine from 14 on keeps its favourites in a settings byte of its own: ADPCM (1.8, 8
 * presets) in factory[15][13], a byte no firmware before it used (kept there as saved) */
#define FAV_ENGINES 14u
typedef struct {
    uint8_t factory[16][32]; /* engine 0..15, preset 0..255 */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites __attribute__((section(".pool")));
static uint8_t *fav_bit(uint32_t engine, uint32_t preset, uint8_t *mask)   /* the byte and bit of a factory preset */
{
    if (engine < NENGINES && engine < FAV_ENGINES && preset < 256u) {
        *mask = (uint8_t)(1u << (preset % 8u));
        return &favorites.factory[engine][preset / 8u];
    }
#if FELUCCA_ADPCM
    if (engine == ENGI_ADPCM && preset < 8u) {
        *mask = (uint8_t)(1u << preset);
        return &favorites.factory[15][13];
    }
#endif
    return 0;
}
static int favorite_has(uint32_t engine, uint32_t preset)
{
    uint8_t m, *b;
    if (engine == NENGINES)
        return preset < UP_SLOTS && ((favorites.user >> preset) & 1u);
    b = fav_bit(engine, preset, &m);
    return b && (*b & m);
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    uint8_t m, *b;
    if (engine == NENGINES) {
        if (preset >= UP_SLOTS || favorite_has(engine, preset) == !!on)
            return 0;
        if (on) favorites.user |= 1u << preset;
        else favorites.user &= ~(1u << preset);
        return 1;
    }
    b = fav_bit(engine, preset, &m);
    if (!b || !!(*b & m) == !!on)
        return 0;
    if (on) *b |= m;
    else *b &= (uint8_t)~m;
    return 1;
}

/* MENU > DISPLAY > COLOR's 11th value, CUSTOM (1.6, #244, Discussion #239): a palette of the user's, five RGB565 colours
 * (color1 color2 color3 primary secondary, 16 bits little endian) in favorites.factory[15][0..9], flags in [12] (bit 0: one is
 * stored, bit 1: it is the palette in use, bit 2: the 1.7 layout), set from the editor (PALETTE_SET, editor_palette.c) and saved with the settings. settings.palette
 * stays a built-in palette (the one in use before, or after): firmware before 1.6 shows that and keeps the bytes. The value
 * is in COLOR only while one is stored. Zero (every older setting) = none */
#define ui_cpal (&favorites.factory[15][0])
#define ui_cpal_flags (favorites.factory[15][12])
#define CPAL_STORED 1u
#define CPAL_ACTIVE 2u
#define CPAL_V2 4u                    /* the five are color1 color2 color3 primary secondary (1.7); without: BG SURF TEXT THEME ACCENT */
/* a CUSTOM stored by 1.6 and before, read as the new five: (BG, THEME, TEXT, a red for the BG, ACCENT) */
static void cpal_fix(void)
{
    uint8_t *b = ui_cpal;
    uint32_t bg, rec;
    if (!(ui_cpal_flags & CPAL_STORED) || (ui_cpal_flags & CPAL_V2))
        return;
    bg = (uint32_t)(b[0] | b[1] << 8);
    rec = ux_luma((uint16_t)bg) > 128u ? UI_REC_LIGHT : UI_REC_DARK;
    b[2] = b[6]; b[3] = b[7];                         /* color2 = the THEME */
    b[6] = (uint8_t)rec; b[7] = (uint8_t)(rec >> 8);  /* primary */
    ui_cpal_flags |= CPAL_V2;
}
static uint32_t cpal_on(void) { return (ui_cpal_flags & (CPAL_STORED | CPAL_ACTIVE)) == (CPAL_STORED | CPAL_ACTIVE); }
static uint32_t palette_n(void) { return NPALETTES + ((ui_cpal_flags & CPAL_STORED) ? 1u : 0u); }     /* COLOR's values */
static uint32_t palette_cur(void) { return cpal_on() ? NPALETTES : settings.palette % NPALETTES; }
static const char *palette_name(uint32_t v) { return v == NPALETTES ? "CUSTOM" : UI_PALETTES[v % NPALETTES].name; }
/* the palette in use into gfx.c (the settings changed under it: a start, an import, a restore, a STYLE change) */
static void palette_apply(void)
{
    cpal_fix();
    if (cpal_on()) {
        palette_custom_load(ui_cpal);
        palette_set(NPALETTES);
    } else {
        palette_set(settings.palette % NPALETTES);
    }
}

/* the five colours into the settings' bytes (stored; not yet the palette in use: palette_choose / palette_apply) */
static void cpal_store(const uint16_t *c)
{
    uint32_t i;
    for (i = 0; i < 5u; i++) {
        ui_cpal[2u * i] = (uint8_t)c[i];
        ui_cpal[2u * i + 1u] = (uint8_t)(c[i] >> 8);
    }
    ui_cpal_flags |= CPAL_STORED | CPAL_V2;
}
