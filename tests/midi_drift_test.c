/* SPDX-License-Identifier: GPL-3.0-only
 * Long-run external-clock phase measured against independent wire pulse counts.
 * Use rational musical positions, not another copy of the firmware clock math. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <math.h>

static void drift_packet(uint32_t source, uint32_t status, uint32_t ms)
{
    fm1_ms = ms;
    if (source == 2u) um_byte(status);
    else midi_enqueue(0xFu | status << 8, source);
    events_block(CTL);
}
static void drift_setup(uint32_t source, uint32_t bpm)
{
    static const int16_t len[] = {16,17,23,64}, div[] = {2,3,5,6}, rate[] = {2,3,5,0};
    ui_power_on(); memset(&midi_clock,0,sizeof midi_clock); memset(&um,0,sizeof um);
    mi_r=mi_w=0; midi_beat_samples=0; fm1_ms=0;
    song.g[G_CLOCK]=(int16_t)source; song.g[G_BPM]=(int16_t)bpm;
    events_block(CTL);
    for(uint32_t k=0;k<NTRK;k++) {
        track_t *t=&trk[k];
        set_engine_of(t,0); apply_preset_to(t,0); t->engine=t->eng_req;
        memset(t->step,0,sizeof t->step);
        t->p[P_SLEN]=len[k]; t->p[P_SDIV]=div[k]; t->p[P_SSWING]=0;
    }
    drift_packet(source,0xFA,10);
    for(uint32_t k=0;k<NTRK;k++) {
        trk[k].p[P_AMODE]=AM_UP; trk[k].p[P_ARATE]=rate[k]; trk[k].p[P_ASWING]=0;
        input_on(&trk[k],60,100); input_on(&trk[k],64,100);
    }
    drift_packet(source,0xF8,10);
}
static double drift_beats(uint32_t div)
{
    static const double value[]={1.,.5,.25,.125,1./3.,1./6.,2.,4.,8.,16.};
    return value[div];
}
static int drift_run(uint32_t source, uint32_t bpm, uint32_t jitter, uint32_t interpolate)
{
    int bad=0; drift_setup(source,bpm);
    uint32_t steps[NTRK]={0},prev[NTRK]={0};
    uint32_t pulses=bpm*20u*24u;
    for(uint32_t i=1;i<=pulses;i++) {
        static const int8_t noise[]={0,1,-1,1,0,-1,0,-1,1,-1,0,1,1,0,-1,0,1,-1};
        uint32_t at=10u+(uint32_t)((uint64_t)i*60000u/(bpm*24u));
        if(jitter) at=(uint32_t)((int32_t)at+noise[i%18u]);
        if(interpolate) while(fm1_ms+1u<at) { fm1_ms++; events_block(CTL); }
        drift_packet(source,0xF8,at);
        for(uint32_t k=0;k<NTRK;k++) {
            steps[k]+=(trk[k].seq_idx+(uint32_t)trk[k].p[P_SLEN]-prev[k])%(uint32_t)trk[k].p[P_SLEN];
            prev[k]=trk[k].seq_idx;
        }
    }
    for(uint32_t k=0;k<NTRK;k++) {
        track_t *t=&trk[k];
        double expected=(double)pulses/24.;
        double seq=(steps[k]+(double)t->seq_pos/div_samples((uint32_t)t->p[P_SDIV]))*drift_beats((uint32_t)t->p[P_SDIV]);
        double arp=(t->arp_idx+(double)t->arp_pos/div_samples((uint32_t)t->p[P_ARATE]))*drift_beats((uint32_t)t->p[P_ARATE]);
        double seq_ms=(seq-expected)*60000./bpm,arp_ms=(arp-expected)*60000./bpm;
        printf("source %u, %u BPM, jitter %u, interpolate %u, T%u: seq %.4f ms, ARP %.4f ms after 20 min\n",
               source,bpm,jitter,interpolate,k+1u,seq_ms,arp_ms);
        bad+=check("sequencer phase stays within 2 ms of counted wire pulses",fabs(seq_ms)<2.);
        bad+=check("ARP phase stays within 2 ms without loop resets",fabs(arp_ms)<2.);
        bad+=check("polymetric lengths, rates and held ARP chords are retained",t->p[P_SLEN]==(int16_t[]){16,17,23,64}[k] && t->nheld==2u);
    }
    bad+=check("long-run transport keeps running without input overflow",song.playing && !midi_in_overflow);
    return bad;
}
static int drift_tempo_edges(uint32_t source)
{
    int bad=0; drift_setup(source,120);
    uint32_t steps[NTRK]={0},prev[NTRK]={0};
    uint64_t us=10000u;
    for(uint32_t i=1;i<=400u;i++) {
        uint32_t bpm=i<29u?120u:i<137u?97u:i<233u?240u:40u;
        us+=60000000u/(bpm*24u);
        drift_packet(source,0xF8,(uint32_t)(us/1000u));
        for(uint32_t k=0;k<NTRK;k++) {
            steps[k]+=(trk[k].seq_idx+(uint32_t)trk[k].p[P_SLEN]-prev[k])%(uint32_t)trk[k].p[P_SLEN];
            prev[k]=trk[k].seq_idx;
        }
    }
    for(uint32_t k=0;k<NTRK;k++) {
        track_t *t=&trk[k];
        double seq=(steps[k]+(double)t->seq_pos/div_samples((uint32_t)t->p[P_SDIV]))*drift_beats((uint32_t)t->p[P_SDIV]);
        double arp=(t->arp_idx+(double)t->arp_pos/div_samples((uint32_t)t->p[P_ARATE]))*drift_beats((uint32_t)t->p[P_ARATE]);
        bad+=check("tempo changes preserve sequence phase against wire pulse count",fabs(seq-400./24.)*1500.<2.);
        bad+=check("tempo changes preserve ARP phase against wire pulse count",fabs(arp-400./24.)*1500.<2.);
    }
    return bad;
}
static int drift_start_and_wrap(uint32_t source)
{
    int bad=0; drift_setup(source,120);
    transport_req=2; events_block(CTL);
    for(uint32_t i=0;i<8u;i++) {
        fm1_ms=i?100u+(i-1u)*21u:100u;
        uint32_t status=i?0xF8u:0xFAu;
        if(source==2u) um_byte(status); else midi_enqueue(0xFu|status<<8,source);
    }
    events_block(CTL);
    bad+=check("Start and seven queued clocks enter the first step before consuming progress",song.playing && trk[0].seq_idx==1u && trk[0].seq_pos==0u);
    drift_setup(source,120);
    midi_clock.pos=midi_clock.rendered=0xFFFFFC18u;
    drift_packet(source,0xF8,31u);
    bad+=check("musical counter wrap preserves forward track progress",midi_clock.pos==0u && trk[0].seq_pos>0u && trk[0].seq_idx==0u);
    return bad;
}

int main(void)
{
    int bad=drift_run(2,120,0,0)+drift_run(1,120,1,1)+drift_run(2,97,1,1)+
            drift_run(1,240,1,1)+drift_run(2,40,0,0);
    bad+=drift_tempo_edges(1)+drift_tempo_edges(2)+drift_start_and_wrap(1)+drift_start_and_wrap(2);
    printf("MIDI LONG-RUN PHASE: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
