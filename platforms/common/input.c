/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A computer's controls for the panel (after panel.c): the keyboard and a gamepad as every desktop frontend maps
 * them (platforms/sdl, platforms/libretro), so the mapping lives here once. A frontend turns its own key codes into
 * INP_K_* (the key's US-layout character, lower case, or an INP_K_ name) and its gamepad's buttons into INP_PAD_*
 * (by position: SOUTH the bottom one), calls inp_key / inp_pad / inp_stick, and inp_sync once a frame.
 *   - the keys F3..G5: Z..slash white, S..' black, then B4..G5 on Q..O;
 *   - the buttons on the number row in the panel's order (FX SCL ENV LFO EDIT GLO, HOME SAVE ARP SEQ PLAY REC), [ ] OCT;
 *   - the encoders: the arrows (SELECT up / down, ALGORITHM right / left), F1..F10 the others, F11 F12 MASTER;
 *   - Tab: the cursor on / off (the arrows move it, Space holds, Return latches, Page Up / Down turn), Escape lets every
 *     latch go;
 *   - the gamepad: the D-pad moves the cursor (it comes on), the left stick the arrow (the one moved last is used);
 *     SOUTH holds (or clicks where the arrow is), WEST latches, EAST lets every latch go, L / R turn, START is PLAY and
 *     SELECT HOME (held while they are). */
enum {
    INP_K_UP = 0x100, INP_K_DOWN, INP_K_LEFT, INP_K_RIGHT, INP_K_TAB, INP_K_ESCAPE, INP_K_RETURN, INP_K_PAGEUP, INP_K_PAGEDOWN,
    INP_K_F1, INP_K_F12 = INP_K_F1 + 11, INP_K_SPACE = ' '
};
enum { INP_PAD_UP, INP_PAD_DOWN, INP_PAD_LEFT, INP_PAD_RIGHT, INP_PAD_SOUTH, INP_PAD_WEST, INP_PAD_EAST, INP_PAD_L, INP_PAD_R,
       INP_PAD_START, INP_PAD_SELECT };

enum { KM_KEY, KM_BTN, KM_ENC, KM_MASTER };
typedef struct { uint16_t code; uint8_t kind, id; int8_t step; } inp_keymap_t;
static const inp_keymap_t INP_KEYS[] = {
    {'z', KM_KEY, 0, 0}, {'x', KM_KEY, 2, 0}, {'c', KM_KEY, 4, 0}, {'v', KM_KEY, 6, 0}, {'b', KM_KEY, 7, 0},
    {'n', KM_KEY, 9, 0}, {'m', KM_KEY, 11, 0}, {',', KM_KEY, 12, 0}, {'.', KM_KEY, 14, 0}, {'/', KM_KEY, 16, 0},
    {'s', KM_KEY, 1, 0}, {'d', KM_KEY, 3, 0}, {'f', KM_KEY, 5, 0}, {'h', KM_KEY, 8, 0}, {'j', KM_KEY, 10, 0},
    {'l', KM_KEY, 13, 0}, {';', KM_KEY, 15, 0}, {'\'', KM_KEY, 17, 0},
    {'q', KM_KEY, 18, 0}, {'w', KM_KEY, 19, 0}, {'e', KM_KEY, 20, 0}, {'r', KM_KEY, 21, 0}, {'t', KM_KEY, 22, 0},
    {'y', KM_KEY, 23, 0}, {'u', KM_KEY, 24, 0}, {'i', KM_KEY, 25, 0}, {'o', KM_KEY, 26, 0},
    {'1', KM_BTN, B_FX, 0}, {'2', KM_BTN, B_SCL, 0}, {'3', KM_BTN, B_ENV, 0}, {'4', KM_BTN, B_LFO, 0},
    {'5', KM_BTN, B_EDIT, 0}, {'6', KM_BTN, B_GLO, 0}, {'7', KM_BTN, B_HOME, 0}, {'8', KM_BTN, B_SAVE, 0},
    {'9', KM_BTN, B_ARP, 0}, {'0', KM_BTN, B_SEQ, 0}, {'-', KM_BTN, B_PLAY, 0}, {'=', KM_BTN, B_REC, 0},
    {'[', KM_BTN, B_OCTDN, 0}, {']', KM_BTN, B_OCTUP, 0},
    {INP_K_UP, KM_ENC, EN_SELECT, 1}, {INP_K_DOWN, KM_ENC, EN_SELECT, -1},
    {INP_K_RIGHT, KM_ENC, EN_ALGO, 1}, {INP_K_LEFT, KM_ENC, EN_ALGO, -1},
    {INP_K_F1, KM_ENC, EN_PRESET, -1}, {INP_K_F1 + 1, KM_ENC, EN_PRESET, 1},
    {INP_K_F1 + 2, KM_ENC, EN_K1, -1}, {INP_K_F1 + 3, KM_ENC, EN_K1, 1},
    {INP_K_F1 + 4, KM_ENC, EN_K2, -1}, {INP_K_F1 + 5, KM_ENC, EN_K2, 1},
    {INP_K_F1 + 6, KM_ENC, EN_K3, -1}, {INP_K_F1 + 7, KM_ENC, EN_K3, 1},
    {INP_K_F1 + 8, KM_ENC, EN_K4, -1}, {INP_K_F1 + 9, KM_ENC, EN_K4, 1},
    {INP_K_F1 + 10, KM_MASTER, 0, -1}, {INP_K_F12, KM_MASTER, 0, 1},
};

typedef struct {
    panel_ui_t *pnl;
    uint32_t key, btn, pad_btn;          /* held on the keyboard, the keyboard's buttons, the pad's buttons */
    uint32_t sent_btn, sent_key;         /* what the device has */
} inp_state_t;

static void inp_init(inp_state_t *s, panel_ui_t *pnl)
{
    s->pnl = pnl;
    s->key = s->btn = s->pad_btn = 0;
    s->sent_btn = s->sent_key = ~0u;
}

/* the keyboard's, the panel's and the pad's together, to the device when they change */
static void inp_sync(inp_state_t *s)
{
    uint32_t b = s->btn | s->pad_btn | panel_buttons(s->pnl), k = s->key | panel_keys(s->pnl);
    if (b != s->sent_btn)
        fh_buttons(s->sent_btn = b);
    if (k != s->sent_key)
        fh_keys(s->sent_key = k);
}

/* a key down or up (repeat: down again while it is down) */
static void inp_key(inp_state_t *s, uint32_t code, int down, int repeat)
{
    panel_ui_t *p = s->pnl;
    uint32_t i;
    if (code == INP_K_TAB) {
        if (down && !repeat)
            panel_cursor_on(p, !p->cursor);
        return;
    }
    if (code == INP_K_ESCAPE) {
        if (down)
            panel_release_all(p);
        return;
    }
    if (p->cursor) {
        int dx = code == INP_K_RIGHT ? 1 : code == INP_K_LEFT ? -1 : 0;
        int dy = code == INP_K_DOWN ? 1 : code == INP_K_UP ? -1 : 0;
        if (dx || dy) {
            if (down)
                panel_cursor_move(p, dx, dy);
            return;
        }
        if (code == INP_K_SPACE) {
            if (!repeat)
                panel_cursor_press(p, down);
            return;
        }
        if (code == INP_K_RETURN) {
            if (down && !repeat)
                panel_cursor_latch(p);
            return;
        }
        if (code == INP_K_PAGEUP || code == INP_K_PAGEDOWN) {
            if (down)
                panel_cursor_turn(p, code == INP_K_PAGEUP ? 1 : -1);
            return;
        }
    }
    for (i = 0; i < sizeof INP_KEYS / sizeof INP_KEYS[0]; i++) {
        const inp_keymap_t *k = &INP_KEYS[i];
        if (k->code != code)
            continue;
        if (k->kind == KM_KEY && !repeat)
            s->key = down ? s->key | 1u << k->id : s->key & ~(1u << k->id);
        else if (k->kind == KM_BTN && !repeat)
            s->btn = down ? s->btn | 1u << k->id : s->btn & ~(1u << k->id);
        else if (k->kind == KM_ENC && down)
            fh_enc(k->id, k->step);
        else if (k->kind == KM_MASTER && down)
            pnl_turn(p, PC_MASTER, 2 * k->step);
        return;
    }
}

/* a gamepad button down or up (INP_PAD_*) */
static void inp_pad(inp_state_t *s, uint32_t button, int down)
{
    panel_ui_t *p = s->pnl;
    switch (button) {
    case INP_PAD_UP:
    case INP_PAD_DOWN:
    case INP_PAD_LEFT:
    case INP_PAD_RIGHT:
        if (down) {
            panel_stick_off(p);
            if (!p->cursor)
                panel_cursor_on(p, 1);
            else
                panel_cursor_move(p, button == INP_PAD_RIGHT ? 1 : button == INP_PAD_LEFT ? -1 : 0,
                                  button == INP_PAD_DOWN ? 1 : button == INP_PAD_UP ? -1 : 0);
        }
        break;
    case INP_PAD_SOUTH:
        if (p->stick)
            panel_stick_click(p, down, 0);
        else {
            if (!p->cursor)
                panel_cursor_on(p, 1);
            panel_cursor_press(p, down);
        }
        break;
    case INP_PAD_WEST:
        if (down && p->stick)
            panel_stick_click(p, 1, 1);
        else if (down)
            panel_cursor_latch(p);
        break;
    case INP_PAD_EAST:
        if (down)
            panel_release_all(p);
        break;
    case INP_PAD_L:
    case INP_PAD_R:
        if (down)
            panel_cursor_turn(p, button == INP_PAD_R ? 1 : -1);
        break;
    case INP_PAD_START:
        s->pad_btn = down ? s->pad_btn | 1u << B_PLAY : s->pad_btn & ~(1u << B_PLAY);
        break;
    case INP_PAD_SELECT:
        s->pad_btn = down ? s->pad_btn | 1u << B_HOME : s->pad_btn & ~(1u << B_HOME);
        break;
    default:
        break;
    }
}

/* the left stick, -1.0 .. 1.0 each way, every frame: past a dead zone it moves the arrow (slow near the middle) */
static void inp_stick(inp_state_t *s, float x, float y)
{
    float m = x * x + y * y;
    if (m >= 0.04f)
        panel_stick_move(s->pnl, x * 12.0f * m, y * 12.0f * m);
}
