<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Polled TRS UART receive events

On WL82, read a receive batch only when RX pending (bit 14) or overtime pending
(bit 11) is set. The AC79NN SDK V1.2.1_2023-12-13 `uartx_isr` in
`cpu/wl82/liba/cpu.a` gates on those events and uses the sequence RDC, `csync`,
combined pending clear, HRXCNT. This polled implementation retains that order
and keeps UART interrupts disabled. CON0, CON1, BAUD and HRXCNT have the 16-bit
widths defined by the SDK's WL82.h; DMA addresses and thresholds remain 32-bit.

An HRXCNT value outside an event is not documented as a new batch. If it retains
the previous count, consuming it again advances the software reader beyond the
bytes actually produced. The regression deliberately models that permitted
no-event value; it does not emulate undocumented silicon latch/ack races.

RXCNT is the event threshold, independent of the ring's wrap addresses. Request
events every 32 bytes rather than only at a full 128-byte ring, leaving room for
arrivals while the existing poll is deferred by rendering. This is complementary
to changing when TIMER5 polls UART: it does not alter the timer, MIDI timestamps,
the MIDI parser, synth parameters, sound storage or interrupt priorities.

`tests/uart_dma_test.c` substitutes register accesses around the production HAL
and parser. It checks pending/empty polls, exact packet order, partial messages,
wraps, running status and realtime. Its legacy control reproduces stale replay;
the corrected path must preserve all packets in continuous/deferred-poll cases.
The full runner executes it normally and with strict ASan/UBSan, without the
general DSP exclusions. Host results are not physical timing or throughput
certification. Hardware observations from a combined candidate do not certify
an isolated package, a maximum MIDI rate or a unique silicon root cause.

SDK source: https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/tree/AC79NN_SDK_V1.2.1_2023-12-13
