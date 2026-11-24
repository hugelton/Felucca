/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * The host API (felucca_host.h) on Felucca 1.8.x (research branch): the browser build's device (web/emu/felucca_web.c:
 * the firmware's sources against a simulated FM-1) under the host's names, so the frontends in platforms/ build on the
 * 1.8 line unchanged. 1.8 has no plugin engines: fh_plugin_add takes none (FELUCCA_NO_PLUGINS). */
#ifndef FELUCCA_NO_PLUGINS
#define FELUCCA_NO_PLUGINS 1
#endif
#include "felucca_host.h"
#include "../../web/emu/felucca_web.c"

FH_API void fh_boot(void) { web_boot(); }
FH_API uint32_t fh_booted(void) { return web_booted; }
FH_API uint32_t fh_sample_rate(void) { return web_sample_rate(); }
FH_API uint32_t fh_now_ms(void) { return web_now_ms(); }
FH_API uint32_t fh_playing(void) { return web_playing(); }

FH_API void fh_render(uint32_t n) { web_render(n); }
FH_API float *fh_out_l(void) { return web_out_l(); }
FH_API float *fh_out_r(void) { return web_out_r(); }

FH_API void fh_buttons(uint32_t labels) { web_buttons(labels); }
FH_API void fh_keys(uint32_t mask) { web_keys(mask); }
FH_API void fh_enc(uint32_t role, int32_t n) { web_enc(role, n); }
FH_API void fh_master(uint32_t v) { web_master(v); }
FH_API uint32_t fh_held_buttons(void) { return web_btn_labels; }
FH_API uint32_t fh_held_keys(void) { return web_key_mask; }
FH_API uint32_t fh_master_level(void) { return web_master_adc; }
FH_API uint32_t fh_midi(uint32_t status, uint32_t d1, uint32_t d2) { return web_midi(status, d1, d2); }

FH_API uint16_t *fh_screen(void) { return web_screen(); }
FH_API uint32_t fh_screen_draws(void) { return web_screen_draws(); }
FH_API uint32_t fh_lit_buttons(void) { return web_lit_buttons(); }
FH_API uint32_t fh_lit_keys(void) { return web_lit_keys(); }
FH_API uint32_t fh_dim_buttons(void) { return web_dim_buttons(); }
FH_API uint32_t fh_dim_keys(void) { return web_dim_keys(); }
FH_API uint32_t fh_dim_level(void) { return web_dim_level(); }
FH_API uint32_t fh_breath_buttons(void) { return web_breath_buttons(); }
FH_API uint32_t fh_breath_keys(void) { return web_breath_keys(); }
FH_API uint32_t fh_mid_keys(void) { return web_mid_keys(); }
FH_API uint8_t *fh_anim_levels(void) { return web_anim_levels(); }

FH_API uint8_t *fh_nor(void) { return web_nor(); }
FH_API uint32_t fh_nor_size(void) { return web_nor_size(); }
FH_API void fh_nor_erase(void) { web_nor_erase(); }
FH_API uint32_t fh_flash_writes(void) { return web_flash_writes_count(); }

FH_API const void *fh_plugin_host(void) { return 0; }
FH_API uint32_t fh_plugin_add(const void *d, uint32_t no) { (void)d, (void)no; return 1u; }

FH_API void fh_test_heavy(void) { web_test_heavy(); }
FH_API uint32_t fh_test_user_preset(uint32_t i) { return web_test_user_preset(i); }
/* engine e's presets p, p + 1, p + 2 (wrapping) on T1..T3 as the heavy song's, T4 its drums; the engine's presets */
FH_API uint32_t fh_test_engine(uint32_t e, uint32_t p)
{
    uint32_t n = ENGINES[e % NENGINES]->npresets, k;
    web_test_heavy();
    for (k = 0; k < 3u; k++) {
        host_preset(&trk[k], (uint8_t)(e % NENGINES), (uint8_t)(n ? (p + k) % n : 0u));
        trk[k].p[P_VOICE] = V_POLY;
        trk[k].p[P_SUS] = 127;
    }
    ui.force = 1;
    return n;
}
