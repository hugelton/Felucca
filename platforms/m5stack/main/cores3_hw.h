/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The CoreS3's side (cores3_hw.c, ESP-IDF): kept in its own file so that the Felucca sources
 * (felucca_m5.c) see none of the IDF headers. */
#ifndef CORES3_HW_H
#define CORES3_HW_H
#include <stdint.h>

#define HW_LCD_W 320u
#define HW_LCD_H 240u

void hw_init(void);                                   /* I2C, AXP2101, AW9523B, the LCD on and cleared */
void hw_lcd_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
void hw_lcd_pixels(const void *p, uint32_t bytes);    /* RGB565, high byte first, into the window */
void hw_lcd_fill(uint32_t n, uint16_t c);             /* n pixels of c (native RGB565) into the window */
void hw_lcd_cmd(uint8_t c);
void hw_audio_init(uint32_t fs);                      /* I2S to the AW88298 */
void hw_audio_write(const int16_t *lr, uint32_t frames);   /* blocks while the DMA is full */
void hw_usb_init(void);                               /* USB audio (usb_audio.c) */
void hw_usb_push(const int16_t *lr, uint32_t frames); /* 44.1 kHz stereo frames to the host */
uint32_t hw_amp_status(void);                         /* AW88298: ID << 16 | SYSST */
int hw_touch(uint32_t *x, uint32_t *y);               /* touches now (0..2), the first one's place on the panel */
uint32_t hw_us(void);
void hw_delay_ms(uint32_t ms);
void hw_lock(void);                                   /* recursive: the FM-1's "interrupts off" */
void hw_unlock(void);
void hw_start(void (*ui)(void), void (*audio)(void)); /* the UI on core 0, the audio on core 1 */

#endif
