# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Felucca's icon set for tools/gen_aa_icons.py (its --table): which Fukiai glyph each icon is, and which icons are
rasterised at which size. Another UI on the same pipeline passes a table of its own with these names:
  LEGACY        icon name of assets/icons.json (the enum's first part, in its order) -> glyph name
  EXTRA         the icons added after them (ICON_X_<NAME>) -> glyph name
  SMALL_EXTRA   EXTRA icons also needed at 12 px (every LEGACY one is)
  NOT_DRAWN     LEGACY names kept in the enum but never rasterised
  HUGE_DEFAULT  the 24 px set;  BIG_DEFAULT  the 16 px set (--big overrides it)
"""

# legacy icon name (assets/icons.json) -> Fukiai glyph name. Chosen from the glyph names only
# (the semantics: ADSR stage icons for ATK/DEC/SUS/REL, filter response for CUT/RES, ...): review the
# result in the icon specimen of the UI renders and adjust the table, nothing else changes.
LEGACY = {
    "generic": "ui_knob", "attack": "function_env_adsr_attack", "decay": "function_env_adsr_decay",
    "sustain": "function_env_adsr_sustain", "release": "function_env_adsr_release", "env": "function_env_adsr_lin",
    "cutoff": "function_filter_lpf", "reso": "function_filter_lpf_peak", "rate": "symbol_speed",
    "lfo_wave": "waveform_sine", "wave": "waveform_variant", "pulse": "waveform_pulse_quarter",
    "shape": "waveform_variant_four", "pitch": "waveform_pitch", "tune": "symbol_tuning",
    "detune": "function_phaseout", "transpose": "control_arrow_up_down", "octave": "control_arrow_up",
    "level": "symbol_volume", "pan": "symbol_pan", "mute": "control_speaker_mute",
    "dist": "function_signal_clip", "drive": "symbol_diode", "fold": "waveform_fold", "bits": "function_mask",
    "chorus": "symbol_waves", "delay": "symbol_echo", "reverb": "symbol_spring", "feedback": "symbol_feedback",
    "arp": "control_arrow_loop", "gate": "function_gate_unipolar", "swing": "symbol_swing",
    "tempo": "symbol_tempo", "scale": "control_arow_scale", "quantize": "symbol_grid",
    "glide": "symbol_transition", "slide": "symbol_rise", "voice": "symbol_primitives_o", "mod": "symbol_modular",
    "noise": "waveform_noise_white", "sub": "symbol_bass", "ratio": "function_multiply",
    "algorithm": "symbol_node", "sample": "symbol_audio", "steps": "symbol_grid_nine", "accent": "symbol_thunder_f",
    "time": "symbol_clock", "phase": "function_phase", "fade": "waveform_fade", "mix": "symbol_combine",
    "keytrack": "symbol_keyboard", "size": "control_arrow_both", "damp": "symbol_weight",
    "tone": "function_filter_band", "sweep": "symbol_warp", "vibrato": "waveform_cos", "loop": "waveform_loop",
    "hold": "symbol_lock_close_f", "order": "symbol_sort", "prob": "symbol_dice",
    "length": "control_arrow_end", "division": "note_quarter", "chip": "symbol_primitives_f",
    "midi": "port_midi", "save": "symbol_download_as", "load": "symbol_folder_open", "clear": "symbol_trash",
    "w_sin": "waveform_sine", "w_tri": "waveform_triangle_ac", "w_saw": "waveform_sawtooth_ac",
    "w_sqr": "waveform_square", "w_pls": "waveform_pulse_quarter", "w_pwm": "waveform_pulse_half",
    "w_sh": "waveform_noise_step", "w_dsin": "function_signal_double", "w_spls": "waveform_pulse_eighth",
    "w_rsaw": "waveform_sawtooth_ac_inv", "w_rtri": "waveform_triangle_dc", "w_rtrp": "waveform_triangle_dc_inv",
    "tape": "symbol_tape", "mouth": "symbol_mouth", "trio": "symbol_primitives_o",
    "drawbar": ("symbol_drawbar", "ui_slider_vertical"), "slice": "symbol_comb", "grain": "symbol_cloud_o", "phys": "symbol_pick",
    "drum": "symbol_drum",
}

# icons the redesign adds (header, dialogs, lists, menu); name -> Fukiai glyph
EXTRA = {
    # (1.1: the ones no screen draws are gone: x_pause x_play_o x_star_o x_check x_power x_doc x_plus x_minus
    #  x_undo x_redo x_palette x_back x_down x_up x_hugelton x_fx x_timer; add one back with its first use)
    "x_play": "control_play_f", "x_stop": "control_stop_f", "x_rec": "control_rec_f", "x_rec_o": "control_rec_o",
    # (1.0.2: the tracks' circled numerals are no longer baked: icons.c trk_icon draws a digit on a cushion)
    "x_usb": "port_usb_c", "x_star": "symbol_star", "x_cog": "symbol_cog", "x_lock": "symbol_lock_close_f",
    "x_folder": "symbol_folder", "x_warn": "symbol_error", "x_speaker": "control_speaker_2", "x_info": "symbol_info_f",
    "x_left": "symbol_carret_left", "x_right": "symbol_carret_right",
    # page and state icons (Fukiai 0.8.0): MIXER / PHRASES / SONG pages, motion, calibration, the battery
    # (battery_2 is not used: the stock level 0..3 maps to battery_0 / 1 / 3 / 4, see ui_draw.c batt_icon)
    "x_mixer": "ui_slider_vertical", "x_pattern": "symbol_grid_nine", "x_song": "symbol_disc",
    "x_motion": "symbol_motion", "x_motion_rec": "symbol_motion_rec", "x_motion_del": "symbol_motion_delete",
    "x_doctor": "symbol_doctor", "x_bat0": "system_battery_0", "x_bat1": "system_battery_1",
    "x_bat3": "system_battery_3", "x_bat4": "system_battery_4", "x_bat_chg": "system_battery_charging",
    # the FX hold layer (ui_draw.c draw_layer, Fukiai 0.8.3) and the HOLD menu row
    "x_knob": "ui_knob", "x_hpf": "function_filter_hpf",
    "x_repeat": "control_arrow_loop", "x_reverse": "control_reverse_f", "x_tstop": "symbol_stop",
    "x_freeze": "symbol_freeze",
    # OCT UP / OCT DN (the harmonizer): stand-ins until Fukiai has a pitch-shift glyph;
    # the first of each tuple the font has
    "x_oct_up": ("symbol_pitch_up", "control_arrow_up"), "x_oct_dn": ("symbol_pitch_down", "control_arrow_down"),
    # a missing sample (the header message, the SLICES page): SAMPLE NOT FOUND
    "x_nofile": "symbol_document_disabled",
    # the MENU's tabs (ui_menu.c, 1.0.5): DISPLAY (CONTROL x_knob, AUDIO x_speaker, 1.2 MIDI midi, SYSTEM x_cog)
    "x_eye": "symbol_eye",
}

# a glyph given as a tuple: the first one the font has (a glyph still to be drawn falls back to a stand-in;
# a missing preferred glyph never breaks the build). WHEEL: symbol_drawbar once Fukiai has it.

# new icons that are also needed at 12 px (list rows, menu values)
SMALL_EXTRA = ["x_star", "x_lock", "x_cog", "x_usb", "x_warn", "x_folder", "x_left", "x_right",
               "x_mixer", "x_pattern", "x_song", "x_motion", "x_motion_rec", "x_motion_del",
               "x_hpf", "x_repeat", "x_reverse", "x_tstop", "x_freeze"]
# legacy names no screen draws (1.1): kept in the enum (assets/icons.json's order), not rasterised (no cell)
NOT_DRAWN = ["tape"]

# the 24 px set: the header battery (Fukiai's battery is a wide, short glyph: at 16 px its body was 12 x 6)
# (up to 1.1.5 also the FX map's effect cells; 1.2's map names the 16 keys' effects instead, ui_layer.c layer_fx)
HUGE_DEFAULT = ["x_bat0", "x_bat1", "x_bat3", "x_bat4", "x_bat_chg", "x_usb"]   # (+ the USB-C plug: as flat)

# the 16 px set: header, footer, dialogs and menu rows (everything else only exists at 12 px)
# (1.0.5: the MENU rows lost their icons, the tabs carry them)
BIG_DEFAULT = ["x_play", "x_stop", "x_rec", "x_rec_o", "x_usb", "x_star",
               "x_cog", "x_warn", "x_speaker", "x_info", "x_eye", "midi",
               "wave", "algorithm", "phase", "bits", "sample", "mouth", "trio", "drawbar", "slice", "grain",
               "phys", "drum", "noise", "mod", "tempo",
               "x_song", "x_motion", "x_motion_del", "x_doctor", "x_knob", "rate", "x_bat0", "x_bat1", "x_bat3", "x_bat4", "x_bat_chg",
               "x_nofile"]
