/* SPDX-License-Identifier: GPL-3.0-only
 * The actual polled UART HAL and running-status parser against an independent
 * DMA-event backend. Only register I/O is substituted. WL82's published driver
 * drains pending RX/timeout events; HRXCNT outside an event is unspecified.
 * This backend deliberately retains the last latch, a permitted no-event value.
 * It is NOT a claim about the chip's undocumented latch/ack race timing.
 * LEGACY_CONTROL is the previous five-operation poll as a negative control.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static uint32_t con_read(void), count_read(void);
static void con_write(uint32_t v), sync_io(void);
#define FM1_UART1_CON_READ() con_read()
#define FM1_UART1_CON_WRITE(v) con_write(v)
#define FM1_UART1_COUNT_READ() count_read()
#define FM1_UART1_SYNC() sync_io()
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"
/* Layout comes from the pinned wl82 SDK WL82.h, not a host register guess. */
_Static_assert(sizeof(FM1_UT1_CON0) == 2, "CON0 is a 16-bit register");
_Static_assert(sizeof(FM1_UT1_CON1) == 2, "CON1 is a 16-bit register");
_Static_assert(sizeof(FM1_UT1_BAUD) == 2, "BAUD is a 16-bit register");
_Static_assert(sizeof(FM1_UT1_HRXCNT) == 2, "HRXCNT is a 16-bit register");
_Static_assert(sizeof(FM1_UT1_RXCNT) == 4, "RXCNT is a 32-bit register");
_Static_assert(sizeof(FM1_UT1_RXSADR) == 4, "RXSADR is a 32-bit register");
_Static_assert(sizeof(FM1_UT1_RXEADR) == 4, "RXEADR is a 32-bit register");
_Static_assert(sizeof(FM1_UT1_OTCNT) == 4, "OTCNT is a 32-bit register");
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) { }

static struct {
    uint32_t producer, available, pending, latched, reads, writes, count_reads;
    uint32_t sequence[8], ns;
} hw;
static uint32_t failures;
static int check(const char *name, int ok)
{
    printf("%-67s %s\n", name, ok ? "ok" : "FAIL");
    failures += !ok;
    return ok;
}
static uint32_t con_read(void) { hw.reads++; return 0x41u | hw.pending; }
static void con_write(uint32_t v)
{
    hw.writes++;
    if (v & 0x80u) {
        if (hw.pending) hw.latched = hw.available;
        if (hw.ns < 8) hw.sequence[hw.ns++] = 1;
    }
    if (v & 0x1400u) {
        hw.pending = 0;
        hw.available = 0;
        if (hw.ns < 8) hw.sequence[hw.ns++] = 3;
    }
}
static void sync_io(void) { if (hw.ns < 8) hw.sequence[hw.ns++] = 2; }
static uint32_t count_read(void)
{
    hw.count_reads++;
    if (hw.ns < 8) hw.sequence[hw.ns++] = 4;
    return hw.latched;
}
static void reset(void)
{
    memset(&hw, 0, sizeof hw); memset(&um, 0, sizeof um);
    memset((void *)um_ring, 0, sizeof um_ring);
    mi_w = mi_r = 0; midi_in_overflow = 0;
}
static void arrive(const uint8_t *bytes, uint32_t n, uint32_t event)
{
    for (uint32_t i = 0; i < n; i++) um_ring[hw.producer++ % UM_RING] = bytes[i];
    hw.available += n; hw.pending |= event;
}
static void poll(void)
{
#ifdef LEGACY_CONTROL
    /* The original fm1_uart1_rx_take, before the pending gate. */
    con_write(con_read() | 0x80u);
    sync_io();
    con_write(con_read() | 0x1400u);
    uart_midi_take(count_read() & 0xffffu);
#else
    uart_midi_poll();
#endif
}
static uint32_t packet(uint8_t status, uint8_t d1, uint8_t d2)
{
    return (status >> 4) | (uint32_t)status << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24;
}
static uint64_t hash_packet(uint64_t hash, uint32_t p)
{
    return (hash ^ p) * UINT64_C(1099511628211);
}
int main(void)
{
    static const uint8_t pair[] = {0x90,48,80,0x80,48,0};
    static const uint8_t cc[] = {0xb0,6,54};
    reset(); hw.latched = 37;
    poll();
    check("no pending: stale count37 never advances cursor or emits MIDI",
          um.rd == 0 && um.bytes == 0 && mi_w == 0 && hw.writes == 0 && hw.count_reads == 0);
    reset(); arrive(pair, sizeof pair, 0x0800u); poll();
    check("timeout pending with UART IRQ off: exact note-on and note-off",
          um.bytes == 6 && um.rd == hw.producer && mi_w == 2 &&
          midi_in_q[0] == packet(0x90,48,80) && midi_in_q[1] == packet(0x80,48,0));
    check("wl82 latch, barrier, combined acknowledge, count order",
          hw.ns == 4 && hw.sequence[0] == 1 && hw.sequence[1] == 2 && hw.sequence[2] == 3 && hw.sequence[3] == 4);
    /* Poison the unwritten suffix with plausible MIDI: old bytes must not replay. */
    for (uint32_t i = 6; i < UM_RING; i += 3) {
        um_ring[i] = 0xb0; if (i+1 < UM_RING) um_ring[i+1] = 72;
        if (i+2 < UM_RING) um_ring[i+2] = 99;
    }
    for (uint32_t i = 0; i < 1000; i++) poll();
    check("1000 empty polls never replay stale bytes as CC72=99",
          um.bytes == 6 && um.rd == 6 && mi_w == 2 && hw.writes == 2 && hw.count_reads == 1);
    reset(); arrive(cc, 3, 0x4000u); poll();
    check("RX full pending without IRQ enable consumes exactly one batch",
          um.bytes == 3 && mi_w == 1 && midi_in_q[0] == packet(0xb0,6,54));
    reset(); arrive(pair, 6, 0x4800u); poll(); poll();
    check("simultaneous RX+timeout flags consume batch once", um.bytes == 6 && mi_w == 2 && um.rd == 6);
    reset(); arrive(pair, 2, 0); poll();
    check("partial DMA without event stays untouched", um.bytes == 0 && mi_w == 0 && hw.available == 2);
    arrive(pair+2, 4, 0x0800u); poll();
    check("later timeout completes partial messages in order", um.bytes == 6 && mi_w == 2 && um.rd == 6);
    /* Many real packet boundaries, both events, wraparound, and no-event polls.
     * Compare against independently assembled wire packets, not parser internals. */
    reset(); uint32_t ok = 1, expect_bytes = 0;
    for (uint32_t step = 0; step < 40000; step++) {
        uint8_t value = (uint8_t)(48 + step % 49);
        uint8_t wire[] = {0x90,48,80,0xb0,99,1,0xb0,98,4,0xb0,6,value,0xb0,99,127,0xb0,98,127,0x80,48,0};
        uint32_t want[] = {packet(0x90,48,80),packet(0xb0,99,1),packet(0xb0,98,4),packet(0xb0,6,value),packet(0xb0,99,127),packet(0xb0,98,127),packet(0x80,48,0)};
        uint32_t pos = 0;
        mi_w = mi_r = 0;
        while (pos < sizeof wire) {
            uint32_t len = 1 + (step + pos) % 11;
            if (len > sizeof wire - pos) len = sizeof wire - pos;
            arrive(wire+pos,len,(step+pos)%2 ? 0x0800u : 0x4000u);
            poll(); poll(); pos += len;
        }
        expect_bytes += sizeof wire;
        ok &= mi_w == 7 && !midi_in_overflow && um.bytes == expect_bytes && um.rd == hw.producer % UM_RING;
        for (uint32_t i = 0; i < 7 && i < mi_w; i++) ok &= midi_in_q[i] == want[i];
        if (!ok) break;
    }
    check("40000 NRPN+note-off streams:840000bytes, every packet exact", ok && um.bytes == 840000u);
    /* A full-rate wire with no idle/timeout. Poll at nominal .5ms, with a
     * 5.12ms audio deferral every1024bytes. Count only actual DMA events.
     * RXCNT32 must give the 128-byte ring enough space during that delay. */
    reset(); uint64_t reference = UINT64_C(1469598103934665603), observed = reference;
    uint32_t queued = 0, max_waiting = 0;
    for (uint32_t step = 0; step < 10000; step++) {
        uint8_t value = (uint8_t)(48 + step % 49);
        uint8_t wire[] = {0x90,48,80,0xb0,99,1,0xb0,98,4,0xb0,6,value,0xb0,99,127,0xb0,98,127,0x80,48,0};
        uint32_t want[] = {packet(0x90,48,80),packet(0xb0,99,1),packet(0xb0,98,4),packet(0xb0,6,value),packet(0xb0,99,127),packet(0xb0,98,127),packet(0x80,48,0)};
        for (uint32_t i = 0; i < 7; i++) reference = hash_packet(reference,want[i]);
        for (uint32_t i = 0; i < sizeof wire; i++) {
            arrive(wire+i,1,0);
            if (hw.available >= FM1_UART1_RX_FRAME) hw.pending |= 0x4000u;
            if (hw.available > max_waiting) max_waiting = hw.available;
            if (hw.producer % 1024u < 16u || hw.producer % 2u) continue;
            poll();
            while (mi_r != mi_w) { observed = hash_packet(observed,midi_in_q[mi_r++ % MQ]); queued++; }
        }
    }
    hw.pending |= 0x0800u; poll();
    while (mi_r != mi_w) { observed = hash_packet(observed,midi_in_q[mi_r++ % MQ]); queued++; }
    check("continuous MIDI + deferred polls:70000packets exact, no ring loss",
          queued == 70000u && observed == reference && um.bytes == 210000u && !um.drops && !midi_in_overflow &&
          um.rd == hw.producer % UM_RING && max_waiting < UM_RING);
    static const uint8_t running[] = {0x90,60,100,62,101,0xf8,64,0xfe,102,0xc1,5,6,0x80,60,0};
    static const uint32_t wanted[] = {0x643c9009,0x653e9009,0x0000f80f,0x66409009,0x0005c10c,0x0006c10c,0x003c8008};
    reset();
    for (uint32_t i = 0; i < sizeof running; i++) { arrive(running+i,1,0x0800u); poll(); poll(); }
    check("DMA chunks preserve running status, realtime and program length",
          mi_w == 7 && !memcmp(midi_in_q,wanted,sizeof wanted) && um.bytes == sizeof running);
    printf("%s\n", failures ? "UART DMA EVENT TEST FAILED" : "UART DMA EVENT TEST PASSED");
    return failures ? 1 : 0;
}
