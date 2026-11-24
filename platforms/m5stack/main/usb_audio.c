/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* USB audio (UAC 2.0, espressif/usb_device_uac on TinyUSB): the master output, stereo 16-bit 48 kHz, to the host as
 * an input device. The audio task pushes its 44.1 kHz renders into a ring (hw_usb_push); the component's mic task asks
 * for 10 ms at a time (usb_read), resampled to 48 kHz. Both sides run on the S3's own clock (the I2S DMA paces the
 * renders, the FreeRTOS tick the reads), so the ring only takes up the jitter; the host's clock is TinyUSB's to
 * follow. */
#include <string.h>
#include "usb_device_uac.h"
#include "cores3_hw.h"

#define RING 4096u                       /* frames (93 ms at 44.1 kHz), a power of two */
#define LAG 1536u                        /* the reader starts this far behind the writer (35 ms) */
#define STEP 60211u                      /* 44100 / 48000 in 16.16 */

static int16_t ring[RING * 2u];
static volatile uint32_t wr;             /* frames written (the audio task) */
static uint32_t rd, ph, open;            /* the reader (the mic task): frame, 16.16 phase between rd and rd + 1 */
static uint32_t underruns;

void hw_usb_push(const int16_t *lr, uint32_t frames)
{
    uint32_t w = wr, k;
    for (k = 0; k < frames; k++, w++) {
        ring[(w % RING) * 2u] = lr[2u * k];
        ring[(w % RING) * 2u + 1u] = lr[2u * k + 1u];
    }
    wr = w;
}

static esp_err_t usb_read(uint8_t *buf, size_t len, size_t *bytes_read, void *ctx)
{
    int16_t *o = (int16_t *)buf;
    uint32_t n = (uint32_t)(len / 4u), k, w = wr;
    (void)ctx;
    if (!open || w - rd > RING - 256u) {            /* the first read, or so far behind that the ring has gone round */
        rd = w - LAG;
        ph = 0;
        open = 1;
    }
    for (k = 0; k < n; k++) {
        uint32_t a = (rd % RING) * 2u, b = ((rd + 1u) % RING) * 2u, f = ph;
        if (w - rd < 2u) {                          /* caught up with the writer: hold the last sample */
            o[2u * k] = ring[a];
            o[2u * k + 1u] = ring[a + 1u];
            underruns++;
            continue;
        }
        o[2u * k] = (int16_t)(ring[a] + (((int32_t)ring[b] - ring[a]) * (int32_t)f >> 16));
        o[2u * k + 1u] = (int16_t)(ring[a + 1u] + (((int32_t)ring[b + 1u] - ring[a + 1u]) * (int32_t)f >> 16));
        ph += STEP;
        rd += ph >> 16;
        ph &= 0xFFFFu;
    }
    *bytes_read = n * 4u;
    return ESP_OK;
}

void hw_usb_init(void)
{
    uac_device_config_t c = {.input_cb = usb_read};
    ESP_ERROR_CHECK(uac_device_init(&c));
}
