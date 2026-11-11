/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM: a drum kit of 8 lanes (drum_voice.c, Felucca's own): KICK, SNARE, CLAP, HAT CL, HAT OP, TOM, RIM,
 * BELL. Before 1.0 it was PHYS's MODEL DRUM; projects and user presets saved then load as this engine
 * (drum_from_phys in core.h).
 *
 * Keys and notes: the keys play the General MIDI drum map with the kick on the first C (C KICK, C# RIM,
 * D SNARE, D# CLAP, F# HAT CL, A# HAT OP, F G A B C D TOMS, C# CYM, G# BELL; OCT -/+ move it), and a note
 * plays its GM drum (35..81: kicks, snares, toms, hats, cymbals, bells, congas, claves; other notes as
 * their octave of 36..47), so GM patterns and MIDI parts play as they did on SAMPLE PERC (retired: its sounds
 * load as this engine, core.h drum_from_perc).
 *
 * Parameters: KIT the kit: STD (Felucca's own: TOM RIM BELL on lanes 6..8, other GM notes their congas, claves,
 * cymbals) or a model kit (80 10 66 55 77, 1.5.6 DEEP TIGHT PERC: every lane that kit's own piece, drum_voice.c DV_KIT; KICK does not
 * apply). Its stored values 1..3 were HAND CYM H+CYM (STD with CONGA CLAVE / CYM on lanes 6..8) until 1.0.4: they
 * play 66 10 77 now (DK_PLAYS), and whatever stores them lands there (params.c enum_orig, param_fit: the knob
 * steps over them, the editor's SET, projects, user presets, motion). KICK the kick (PUNCH, ROUND). TUNE (64: as
 * designed, +-12 semitones), TONE, DECY and SNAP (each lane's extra: the kick's drive, the snare's snappiness,
 * the clap's spread, the hats' noise, the toms' bend, the rim's drive, the bell's strike) move every lane from its designed value. ACC
 * is the accent at full velocity (velocity scales it), DRV a soft clip on every hit (x1..x4, level kept).
 * Each lane has its LEVEL (P_LN0..P_LN7, EDIT > LANES / LANES 2, #97: square law, 100 % the kit as designed, the
 * default, so every older sound plays as it did), on the voice amplitude after the knee (a quieter lane is cleaner).
 * A hit keeps the drum and the variant it was struck with; the knobs move it while it rings.
 *
 * Polyphony: a lane per drum and part, mono: a hit reuses its lane's voice even at another pitch, the 8 lanes ring
 * together. A sounding voice plays one lane (s[0]); the lane remembers its voice (owner), and a voice
 * whose lane was struck again by another voice, or has rung out (-84 dB), ends itself (drum_amp). The
 * voices come from the shared budget (voice.c), up to 8 per part. A closed hat chokes the open one.
 *
 * What applies (engine_t.oneshot): the hit is the envelope. VOICE (always POLY), GLIDE, the ADSR, ENV DEST,
 * LFO DEST PIT / FLT / SHP, the global TUNE and the matrix's PITCH CUT SHP do nothing; LEVEL, PAN, the
 * sends, DIST, the SLICER, LFO DEST AMP, velocity, the matrix's AMP and its E1..E8 (the knobs, per
 * block) do. A released key does not end a hit.
 *
 * State: 8 lanes per part in the part's engine memory (engines.c eng_mem; drum_kit_of: the parameters, coefficients, voice and metal
 * source of each lane). */

/* preset list tuned for a hard techno kit */
static const preset_t DRUM_PRESETS[] = {
    {"TECHNO KIT", DRUM_KIT_E, {0, 100, 127, 98}, 0, 0, FX(0, 0, 0, 18), PAT(12)},
    {"DEEP KIT", {DK_DEEP, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 96}, 0, 0, FX(0, 2, 0, 32), PAT(12)},
    {"TIGHT KIT", {DK_TIGHT, 64, 64, 56, 64, 100, 0, 16}, {0, 100, 127, 96}, 0, 0, FX(0, 0, 0, 12), PAT(12)},
    {"PERC KIT", {DK_PERC, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 96}, 0, 0, FX(0, 0, 0, 24), PAT(12)},
};
