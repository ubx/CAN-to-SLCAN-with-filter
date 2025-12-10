#include "led.h"

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef RGB_LED_PIN
#include "driver/rmt_tx.h"

// WS2812 timing constants for 800 kHz protocol, 50 ns tick (20 MHz RMT resolution)
static const uint16_t T0H = 6;
static const uint16_t T0L = 14;
static const uint16_t T1H = 14;
static const uint16_t T1L = 6;
static const uint32_t TRESET_TICKS = 1200;

// Global RMT TX channel and encoder
static rmt_channel_handle_t ws_tx_channel = nullptr;
static rmt_encoder_handle_t ws_encoder = nullptr;

extern "C" void ws2812_init(void)
{
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t)RGB_LED_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        // 20 MHz -> 50 ns tick to match timing constants below
        .resolution_hz = 20 * 1000 * 1000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .intr_priority = 1,
        .flags = {
            false, // io_loop_back
            false, // io_od_mode
            true,  // allow_pd
            false, // invert_out
            false  // with_dma
        }
    };

    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &ws_tx_channel));
    rmt_copy_encoder_config_t enc_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_config, &ws_encoder));
    ESP_ERROR_CHECK(rmt_enable(ws_tx_channel));
}

extern "C" void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    // Select byte order according to LED wiring/protocol expectation
    // Default is GRB (classic WS2812B), can be overridden via build flag
    uint8_t data[3];
#if defined(WS_ORDER_RGB)
    data[0] = r;
    data[1] = g;
    data[2] = b;
#else
    data[0] = g;
    data[1] = r;
    data[2] = b;
#endif

    // 24 bits per LED + 1 reset item
    rmt_symbol_word_t items[24 + 1];
    int idx = 0;
    for (int i = 0; i < 3; ++i)
    {
        for (int bit = 7; bit >= 0; --bit)
        {
            bool is_one = (data[i] >> bit) & 0x01;
            if (is_one)
            {
                items[idx].duration0 = T1H;
                items[idx].level0 = 1;
                items[idx].duration1 = T1L;
                items[idx].level1 = 0;
            }
            else
            {
                items[idx].duration0 = T0H;
                items[idx].level0 = 1;
                items[idx].duration1 = T0L;
                items[idx].level1 = 0;
            }
            idx++;
        }
    }
    // Reset pulse (low for >= 50 µs)
    items[idx].duration0 = TRESET_TICKS;
    items[idx].level0 = 0;
    items[idx].duration1 = 0;
    items[idx].level1 = 0;

    // Fully initialize rmt_transmit_config_t including flags
    rmt_transmit_config_t transmit_cfg = {
        .loop_count = 0,
        .flags = {
            false, // eot_level
            false  // queue_nonblocking
        }
    };

    // copy encoder expects payload size in BYTES, not number of symbols
    size_t payload_bytes = sizeof(rmt_symbol_word_t) * (size_t)(idx + 1);
    ESP_ERROR_CHECK(rmt_transmit(ws_tx_channel, ws_encoder, items, payload_bytes, &transmit_cfg));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(ws_tx_channel, portMAX_DELAY));
}

#else

// No-op implementations when RGB LED is not configured
extern "C" void ws2812_init(void) {}
extern "C" void ws2812_set_color(uint8_t, uint8_t, uint8_t) {}

#endif // RGB_LED_PIN
