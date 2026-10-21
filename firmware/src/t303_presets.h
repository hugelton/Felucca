/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* the 303 engine's presets (eng_303.c). ACID is X0X's factory 303 (its engine.c defaults); the others are Felucca's.
 * The envelope is the 303's own (ATK .. REL unused), LEGATO (overlapping keys slide). Stored preset numbers index this:
 * append, never reorder */
static const preset_t T303_PRESETS[] = {
    /* name, {CUT, RES, ENV, DCY, ACC, WAVE, DRV, DTYP}, {A D S R}, ENV -> FLT, mono, sends, pattern */
    {"ACID", {48, 100, 85, 64, 100, 0, 45, 1}, {0, 64, 127, 10}, 0, 1, FX(0, 0, 40, 12), PAT(1)},
    {"CLASSIC", {64, 64, 64, 64, 64, 0, 0, 0}, {0, 64, 127, 10}, 0, 1, FX(0, 0, 24, 16), PAT(1)},
    {"SQUELCH", {30, 118, 110, 40, 110, 0, 20, 0}, {0, 64, 127, 10}, 0, 1, FX(0, 0, 32, 12), PAT(1)},
    {"SQR BASS", {40, 70, 50, 50, 80, 1, 0, 0}, {0, 64, 127, 10}, 0, 1, FX(0, 0, 0, 12), PAT(2)},
    {"RUBBER", {20, 90, 100, 90, 90, 1, 30, 0}, {0, 64, 127, 10}, 0, 1, FX(0, 0, 20, 12), PAT(1)},
    {"DIST ACID", {56, 110, 95, 70, 120, 0, 90, 1}, {0, 64, 127, 10}, 0, 1, FX(0, 0, 40, 12), PAT(1)},
    {"DEEP", {16, 40, 30, 100, 60, 0, 0, 0}, {0, 64, 127, 10}, 0, 1, FX(0, 0, 0, 12), PAT(8)},
    {"SCREAM", {70, 124, 127, 30, 127, 0, 70, 1}, {0, 64, 127, 10}, 0, 1, FX(0, 0, 48, 20), PAT(1)},
};
