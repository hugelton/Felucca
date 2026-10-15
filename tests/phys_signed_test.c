/* SPDX-License-Identifier: GPL-3.0-only */
/* PHYS negative output: excite the actual track, freeze baseline samples and
 * require the same render under strict ASan/UBSan. */
#define main hostsim_main
#include "hostsim.c"
#undef main
int main(void)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t negative = 0, positive = 0;
    host_tracks_init(); host_preset(&trk[0], ENGI_PHYS, 0);
    trk_note_on(&trk[0], 60, 80);
    for (uint32_t b = 0; b < 128; b++) {
        int32_t out[CTL] = {0}; track_render(&trk[0], out, CTL);
        for (uint32_t i = 0; i < CTL; i++) {
            negative += out[i] < 0; positive += out[i] > 0;
            hash = (hash ^ (uint32_t)out[i]) * UINT64_C(1099511628211);
        }
    }
    printf("PHYS signed: negative=%u positive=%u hash=%016llx\n", negative, positive, (unsigned long long)hash);
    return !negative || !positive || hash != UINT64_C(0x43636377a989c407);
}
