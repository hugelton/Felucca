/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI in for platforms/linux/felucca_sdl.c: channel messages into the device as USB MIDI packets arrive
 * (felucca_web.c web_midi: notes, CC, program change, pressure, pitch bend; not the clock or SysEx yet).
 *   - Linux, FELUCCA_ALSA (build.sh: when pkg-config finds alsa): an ALSA sequencer port "Felucca", subscribed to
 *     every source there at start (others: aconnect);
 *   - macOS: CoreMIDI, a virtual destination "Felucca" and every source there at start;
 *   - else: none.
 * midi_poll runs on the main loop (the device is the main loop's only); CoreMIDI's thread only fills a ring. */
static uint32_t midi_count;                     /* messages into the device (felucca_sdl.c --midi-check) */
#define MIDI_IN(st, d1, d2) (midi_count++, web_midi((st), (d1), (d2)))

#if defined(__APPLE__)
#include <CoreMIDI/CoreMIDI.h>

static uint8_t midi_ring[4096];
static volatile uint32_t midi_w, midi_r;        /* (one writer: CoreMIDI's thread; one reader: the main loop) */
static MIDIClientRef midi_client;
static MIDIPortRef midi_port;
static MIDIEndpointRef midi_dest;

static void midi_read(const MIDIPacketList *pl, void *a, void *b)
{
    const MIDIPacket *p = &pl->packet[0];
    uint32_t i, j;
    (void)a, (void)b;
    for (i = 0; i < pl->numPackets; i++, p = MIDIPacketNext(p))
        for (j = 0; j < p->length; j++) {
            uint32_t w = __atomic_load_n(&midi_w, __ATOMIC_RELAXED);
            if (w - __atomic_load_n(&midi_r, __ATOMIC_ACQUIRE) >= sizeof midi_ring)
                return;                         /* (full: dropped) */
            midi_ring[w % sizeof midi_ring] = p->data[j];
            __atomic_store_n(&midi_w, w + 1u, __ATOMIC_RELEASE);
        }
}
static void midi_open(void)
{
    ItemCount i, n;
    if (MIDIClientCreate(CFSTR("Felucca"), 0, 0, &midi_client) != noErr ||
        MIDIInputPortCreate(midi_client, CFSTR("Felucca in"), midi_read, 0, &midi_port) != noErr) {
        fprintf(stderr, "felucca: no MIDI (CoreMIDI)\n");
        midi_client = 0;
        return;
    }
    MIDIDestinationCreate(midi_client, CFSTR("Felucca"), midi_read, 0, &midi_dest);
    for (i = 0, n = MIDIGetNumberOfSources(); i < n; i++)
        MIDIPortConnectSource(midi_port, MIDIGetSource(i), 0);
    fprintf(stderr, "felucca: MIDI in: \"Felucca\" and %u sources\n", (unsigned)n);
}
static int midi_byte(uint8_t *b)
{
    uint32_t r = __atomic_load_n(&midi_r, __ATOMIC_RELAXED);
    if (r == __atomic_load_n(&midi_w, __ATOMIC_ACQUIRE))
        return 0;
    *b = midi_ring[r % sizeof midi_ring];
    __atomic_store_n(&midi_r, r + 1u, __ATOMIC_RELEASE);
    return 1;
}
static void midi_poll(void)                     /* the bytes, running status, into messages */
{
    static uint8_t st, d[2], nd, sysex;
    uint8_t b;
    while (midi_byte(&b)) {
        if (b >= 0xF8u)                         /* (real time: the clock, not yet) */
            continue;
        if (b >= 0x80u) {
            sysex = b == 0xF0u;
            st = b < 0xF0u ? b : 0;
            nd = 0;
            continue;
        }
        if (sysex || !st)
            continue;
        d[nd++] = b;
        if (nd == ((st & 0xE0u) == 0xC0u ? 1u : 2u)) {     /* (program change, channel pressure: one byte) */
            MIDI_IN(st, d[0], nd > 1u ? d[1] : 0u);
            nd = 0;
        }
    }
}
static void midi_close(void)
{
    if (midi_client)
        MIDIClientDispose(midi_client);
    midi_client = 0;
}

#elif defined(FELUCCA_ALSA)
#include <alsa/asoundlib.h>

static snd_seq_t *midi_seq;

static void midi_open(void)
{
    snd_seq_client_info_t *ci;
    snd_seq_port_info_t *pi;
    int port, self, n = 0;
    if (snd_seq_open(&midi_seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
        fprintf(stderr, "felucca: no MIDI (ALSA sequencer)\n");
        midi_seq = 0;
        return;
    }
    snd_seq_set_client_name(midi_seq, "Felucca");
    port = snd_seq_create_simple_port(midi_seq, "Felucca", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                      SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    self = snd_seq_client_id(midi_seq);
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (port >= 0 && snd_seq_query_next_client(midi_seq, ci) >= 0) {   /* every source there now */
        int c = snd_seq_client_info_get_client(ci);
        if (c == SND_SEQ_CLIENT_SYSTEM || c == self)
            continue;
        snd_seq_port_info_set_client(pi, c);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(midi_seq, pi) >= 0) {
            unsigned cap = snd_seq_port_info_get_capability(pi);
            if ((cap & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) == (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ) &&
                !(cap & SND_SEQ_PORT_CAP_NO_EXPORT) && snd_seq_connect_from(midi_seq, port, c, snd_seq_port_info_get_port(pi)) >= 0)
                n++;
        }
    }
    fprintf(stderr, "felucca: MIDI in: ALSA %d:%d and %d sources\n", self, port, n);
}
static void midi_poll(void)
{
    snd_seq_event_t *ev;
    while (midi_seq && snd_seq_event_input(midi_seq, &ev) >= 0 && ev) {
        const snd_seq_ev_ctrl_t *c = &ev->data.control;
        const snd_seq_ev_note_t *n = &ev->data.note;
        switch (ev->type) {
        case SND_SEQ_EVENT_NOTEON: MIDI_IN(0x90u | (n->channel & 15u), n->note, n->velocity); break;
        case SND_SEQ_EVENT_NOTEOFF: MIDI_IN(0x80u | (n->channel & 15u), n->note, n->velocity); break;
        case SND_SEQ_EVENT_KEYPRESS: MIDI_IN(0xA0u | (n->channel & 15u), n->note, n->velocity); break;
        case SND_SEQ_EVENT_CONTROLLER: MIDI_IN(0xB0u | (c->channel & 15u), c->param & 127u, (uint32_t)c->value & 127u); break;
        case SND_SEQ_EVENT_PGMCHANGE: MIDI_IN(0xC0u | (c->channel & 15u), (uint32_t)c->value & 127u, 0); break;
        case SND_SEQ_EVENT_CHANPRESS: MIDI_IN(0xD0u | (c->channel & 15u), (uint32_t)c->value & 127u, 0); break;
        case SND_SEQ_EVENT_PITCHBEND: {
            uint32_t v = (uint32_t)(c->value + 8192) & 0x3FFFu;
            MIDI_IN(0xE0u | (c->channel & 15u), v & 127u, v >> 7);
            break;
        }
        }
    }
}
static void midi_close(void)
{
    if (midi_seq)
        snd_seq_close(midi_seq);
    midi_seq = 0;
}

#else
static void midi_open(void) { fprintf(stderr, "felucca: no MIDI in this build\n"); }
static void midi_poll(void) {}
static void midi_close(void) {}
#endif
