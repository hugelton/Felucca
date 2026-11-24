/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* M5Stack CoreS3: the power chip, the IO expander, the LCD (ILI9342 on SPI) and the speaker amplifier (AW88298
 * on I2S). The register values follow M5Unified / M5GFX (MIT) for this board. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cores3_hw.h"

#define PIN_SDA 12
#define PIN_SCL 11
#define PIN_LCD_MOSI 37
#define PIN_LCD_SCK 36
#define PIN_LCD_DC 35
#define PIN_LCD_CS 3
#define PIN_I2S_BCK 34
#define PIN_I2S_WS 33
#define PIN_I2S_DOUT 13

#define AXP2101 0x34
#define AW9523 0x58
#define AW88298 0x36
#define FT6336U 0x38

#define LCD_CHUNK 32000u                 /* bytes per SPI transfer (the S3's limit is 32 KB) */

static const char *TAG = "cores3";
static i2c_master_bus_handle_t i2c;
static i2c_master_dev_handle_t dev_axp, dev_aw, dev_amp, dev_touch;
static spi_device_handle_t lcd;
static i2s_chan_handle_t tx;
static DMA_ATTR uint16_t fillbuf[HW_LCD_W * 8u];

static void reg8(i2c_master_dev_handle_t d, uint8_t r, uint8_t v)
{
    uint8_t b[2] = {r, v};
    ESP_ERROR_CHECK(i2c_master_transmit(d, b, 2, 100));
}

static void reg16(i2c_master_dev_handle_t d, uint8_t r, uint16_t v)  /* AW88298: 16-bit registers, high byte first */
{
    uint8_t b[3] = {r, (uint8_t)(v >> 8), (uint8_t)v};
    ESP_ERROR_CHECK(i2c_master_transmit(d, b, 3, 100));
}

static i2c_master_dev_handle_t i2c_dev(uint16_t addr)
{
    i2c_device_config_t c = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
    i2c_master_dev_handle_t d;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c, &c, &d));
    return d;
}

static void lcd_tx(const void *p, uint32_t n, int dc)
{
    const uint8_t *s = p;
    gpio_set_level(PIN_LCD_DC, dc);
    while (n) {
        uint32_t k = n > LCD_CHUNK ? LCD_CHUNK : n;
        spi_transaction_t t = {.length = k * 8u, .tx_buffer = s};
        ESP_ERROR_CHECK(spi_device_polling_transmit(lcd, &t));
        s += k;
        n -= k;
    }
}

void hw_lcd_cmd(uint8_t c) { lcd_tx(&c, 1, 0); }

static void lcd_cmd_data(uint8_t c, const uint8_t *d, uint32_t n)
{
    hw_lcd_cmd(c);
    if (n)
        lcd_tx(d, n, 1);
}

void hw_lcd_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    uint8_t a[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t b[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    lcd_cmd_data(0x2A, a, 4);
    lcd_cmd_data(0x2B, b, 4);
    hw_lcd_cmd(0x2C);
}

void hw_lcd_pixels(const void *p, uint32_t bytes) { lcd_tx(p, bytes, 1); }

void hw_lcd_fill(uint32_t n, uint16_t c)
{
    uint32_t i, k = sizeof fillbuf / 2u;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    for (i = 0; i < k; i++)
        fillbuf[i] = sw;
    for (; n; n -= k) {
        if (k > n)
            k = n;
        lcd_tx(fillbuf, k * 2u, 1);
    }
}

static void lcd_init(void)
{
    static const uint8_t colmod = 0x55, madctl = 0x08;   /* RGB565; ILI9342: landscape, BGR panel */
    spi_bus_config_t bus = {.mosi_io_num = PIN_LCD_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_LCD_SCK,
                            .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = LCD_CHUNK};
    spi_device_interface_config_t dev = {.clock_speed_hz = 40000000, .mode = 0, .spics_io_num = PIN_LCD_CS,
                                         .queue_size = 1};
    gpio_config_t dc = {.pin_bit_mask = 1ull << PIN_LCD_DC, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&dc));
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &lcd));
    reg8(dev_aw, 0x03, 0x81);                          /* AW9523B P1.1: LCD reset low */
    vTaskDelay(pdMS_TO_TICKS(20));
    reg8(dev_aw, 0x03, 0x83);                          /* released */
    vTaskDelay(pdMS_TO_TICKS(120));
    hw_lcd_cmd(0x01);                                  /* SWRESET */
    vTaskDelay(pdMS_TO_TICKS(150));
    hw_lcd_cmd(0x11);                                  /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd_data(0x3A, &colmod, 1);
    lcd_cmd_data(0x36, &madctl, 1);
    hw_lcd_cmd(0x21);                                  /* INVON: IPS */
    hw_lcd_cmd(0x13);                                  /* NORON */
    hw_lcd_window(0, 0, HW_LCD_W - 1u, HW_LCD_H - 1u);
    hw_lcd_fill(HW_LCD_W * HW_LCD_H, 0);
    hw_lcd_cmd(0x29);                                  /* DISPON */
}

void hw_init(void)
{
    static const uint8_t AXP[][2] = {
        {0x90, 0xBF},                                  /* LDOs on: ALDO1-4, BLDO1-2, DLDO1 (backlight) */
        {0x92, 13},                                    /* ALDO1 1.8 V: AW88298 */
        {0x93, 28},                                    /* ALDO2 3.3 V: ES7210 */
        {0x94, 28},                                    /* ALDO3 3.3 V: camera */
        {0x95, 28},                                    /* ALDO4 3.3 V: SD */
        {0x27, 0x00}, {0x69, 0x11}, {0x10, 0x30}, {0x30, 0x0F},
        {0x99, 24},                                    /* DLDO1: backlight level (0.5 V + 0.1 V x n) */
    };
    static const uint8_t AW[][2] = {
        {0x02, 0x07},                                  /* P0: touch reset, bus out, AW88298 reset released */
        {0x03, 0x83},                                  /* P1: camera, LCD reset released, boost on */
        {0x04, 0x18}, {0x05, 0x0C},                    /* directions */
        {0x11, 0x10},                                  /* P0 push-pull */
        {0x12, 0xFF}, {0x13, 0xFF},                    /* GPIO mode, not LED */
    };
    i2c_master_bus_config_t bc = {.i2c_port = I2C_NUM_0, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
                                  .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                  .flags.enable_internal_pullup = true};
    uint32_t i;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &i2c));
    dev_axp = i2c_dev(AXP2101);
    dev_aw = i2c_dev(AW9523);
    dev_amp = i2c_dev(AW88298);
    dev_touch = i2c_dev(FT6336U);
    for (i = 0; i < sizeof AXP / sizeof AXP[0]; i++)
        reg8(dev_axp, AXP[i][0], AXP[i][1]);
    for (i = 0; i < sizeof AW / sizeof AW[0]; i++)
        reg8(dev_aw, AW[i][0], AW[i][1]);
    lcd_init();
    ESP_LOGI(TAG, "LCD on");
}

void hw_audio_init(uint32_t fs)
{
    static const uint8_t RATE[] = {4, 5, 6, 8, 10, 11, 15, 20, 22, 44};   /* AW88298 reg 0x06: rate / 2205 */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(fs),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = PIN_I2S_BCK, .ws = PIN_I2S_WS, .dout = PIN_I2S_DOUT,
                     .din = I2S_GPIO_UNUSED},
    };
    uint32_t r = (fs + 1102u) / 2205u, k = 0;
    while (k < sizeof RATE - 1u && r > RATE[k])
        k++;
    reg16(dev_amp, 0x61, 0x0673);                      /* boost off */
    reg16(dev_amp, 0x04, 0x4040);                      /* I2S on, amp on */
    reg16(dev_amp, 0x05, 0x0008);                      /* unmuted */
    reg16(dev_amp, 0x06, (uint16_t)(0x14C0u | k));     /* 16 x 2 BCK, the rate */
    reg16(dev_amp, 0x0C, 0x0064);                      /* volume (M5Unified: the amplifier first, then the I2S) */
    cc.dma_desc_num = 8;                               /* 8 x 512 frames: ~93 ms, room for a slow UI frame */
    cc.dma_frame_num = 512;
    cc.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx, NULL));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &sc));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_LOGI(TAG, "audio %lu Hz", (unsigned long)fs);
}

int hw_touch(uint32_t *x, uint32_t *y)                /* FT6336U: touches (0..2), the first one's place */
{
    uint8_t r = 0x02, b[5] = {0};
    if (i2c_master_transmit_receive(dev_touch, &r, 1, b, 5, 20) != ESP_OK)
        return 0;
    *x = (uint32_t)(b[1] & 0x0Fu) << 8 | b[2];
    *y = (uint32_t)(b[3] & 0x0Fu) << 8 | b[4];
    return b[0] & 0x0F;
}

void IRAM_ATTR hw_audio_write(const int16_t *lr, uint32_t frames)
{
    size_t done;
    i2s_channel_write(tx, lr, frames * 4u, &done, portMAX_DELAY);
}

uint32_t hw_amp_status(void)                           /* AW88298: ID (0x1852) << 16 | SYSST */
{
    uint8_t r = 0x00, v[2] = {0, 0}, s[2] = {0, 0};
    i2c_master_transmit_receive(dev_amp, &r, 1, v, 2, 100);
    r = 0x01;
    i2c_master_transmit_receive(dev_amp, &r, 1, s, 2, 100);
    return (uint32_t)v[0] << 24 | (uint32_t)v[1] << 16 | (uint32_t)s[0] << 8 | s[1];
}

uint32_t hw_us(void) { return (uint32_t)esp_timer_get_time(); }
void hw_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static SemaphoreHandle_t lock;
void hw_lock(void) { xSemaphoreTakeRecursive(lock, portMAX_DELAY); }
void hw_unlock(void) { xSemaphoreGiveRecursive(lock); }

static void task(void *arg)
{
    ((void (*)(void))arg)();
    vTaskDelete(NULL);
}

void hw_start(void (*ui)(void), void (*audio)(void))
{
    lock = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(task, "felucca_ui", 16384, (void *)ui, 5, NULL, 0);
    xTaskCreatePinnedToCore(task, "felucca_audio", 8192, (void *)audio, 20, NULL, 1);
}
