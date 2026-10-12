/* SPDX-License-Identifier: GPL-3.0-only */
/* Render the real ANALOG engine in isolation under strict sanitizers: negative
 * samples, PWM shape below/above centre, all waves/filter taps, resonance/drive
 * corners and amplitude ramps. The frozen hash comes from the pre-fix engine;
 * --dump is an explicit comparison tool, never the suite's acceptance mode. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint64_t hash = UINT64_C(0xcbf29ce484222325);
static void word(uint32_t x)
{
    for (uint32_t k = 0; k < 4; k++) {
        hash ^= (x >> (8 * k)) & 255u;
        hash *= UINT64_C(0x100000001b3);
    }
}
int main(int argc, char **argv)
{
    static const int32_t shapes[] = {0, 48 * 256, 64 * 256, 80 * 256, 128 * 256};
    static const int32_t notes[] = {24, 60, 108};
    static const int32_t corners[] = {0, 64, 127};
    uint32_t cases = 0, negative = 0, positive = 0;
    host_tracks_init();
    for (uint32_t w = 0; w < 7; w++)
        for (uint32_t ft = 0; ft < 3; ft++)
            for (uint32_t shape = 0; shape < NELEM(shapes); shape++)
                for (uint32_t note = 0; note < NELEM(notes); note++)
                    for (uint32_t corner = 0; corner < NELEM(corners); corner++)
                        for (uint32_t ramp = 0; ramp < 2; ramp++) {
                            track_t *t = &trk[0];
                            host_preset(t, 0, 0);
                            t->p[P_E0] = (int16_t)w;
                            t->p[P_FTYPE] = (int16_t)ft;
                            t->p[P_E1] = 127;
                            t->p[P_E2] = 64;
                            t->p[P_E3] = (int16_t)corners[corner];
                            t->p[P_E4] = (int16_t)corners[corner];
                            t->p[P_E5] = (int16_t)corners[corner];
                            t->p[P_E6] = (int16_t)corners[corner];
                            t->p[P_E7] = 127;
                            voice_t v = {0};
                            v.pitch16 = notes[note] * 16;
                            analog_note_on(t, &v);
                            vmod_t m = {0};
                            m.pitch16 = v.pitch16;
                            m.inc = pitch_inc(m.pitch16);
                            m.shape = shapes[shape];
                            m.amp0 = ramp ? 32767 : 8192;
                            m.amp1 = ramp ? 0 : 8192;
                            word(w); word(ft); word(shape); word(note); word(corner); word(ramp);
                            for (uint32_t block = 0; block < 12; block++) {
                                int32_t out[CTL] = {0};
                                analog_render(t, &v, out, CTL, &m);
                                for (uint32_t i = 0; i < CTL; i++) {
                                    word((uint32_t)out[i]);
                                    negative += out[i] < 0;
                                    positive += out[i] > 0;
                                    if (out[i] < -4 * VOICE_FS || out[i] > 4 * VOICE_FS) {
                                        fprintf(stderr, "ANALOG sample %d outside bound: wave=%u ft=%u shape=%u note=%u corner=%u ramp=%u block=%u\n",
                                                out[i], w, ft, shape, note, corner, ramp, block);
                                        return 1;
                                    }
                                }
                            }
                            cases++;
                        }
    printf("ANALOG strict sweep: %u cases, negative=%u positive=%u hash=%016llx\n",
           cases, negative, positive, (unsigned long long)hash);
    if (!negative || !positive || cases != 1890u)
        return 1;
    if (argc == 2 && !strcmp(argv[1], "--dump"))
        return 0;
    if (hash != UINT64_C(0xaea76a7387be2d18)) {
        fprintf(stderr, "ANALOG baseline audio hash mismatch\n");
        return 1;
    }
    puts("ANALOG strict signed-scaling regression passed");
    return 0;
}
