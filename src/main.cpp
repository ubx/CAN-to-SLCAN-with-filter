#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "whitelist.h"

#ifndef APP_NAME
#define APP_NAME "UnknownApp"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

#ifndef GIT_REVISION
#define GIT_REVISION "unknown"
#endif

static const char* TAG = "twai_slcan_cpp";

#ifndef TWAI_TX_GPIO
#define TWAI_TX_GPIO 18
#endif

#ifndef TWAI_RX_GPIO
#define TWAI_RX_GPIO 17
#endif
#define TWAI_TIMING TWAI_TIMING_CONFIG_500KBITS()
#define SLCAN_MAX_FRAME_LEN 32

static inline char nibble_to_hex(uint8_t n)
{
    n &= 0xF;
    return (n < 10) ? ('0' + n) : ('A' + (n - 10));
}

#ifdef RGB_LED_PIN
#include "driver/rmt.h"
// Timing per WS2812B at 800 kHz. We use RMT with 50 ns tick (APB 80MHz / clk_div 4)
static const int WS_RMT_CHANNEL = RMT_CHANNEL_0;
static const int WS_RMT_CLK_DIV = 4; // 80MHz / 4 = 20MHz => 50ns tick
static const uint16_t T0H = 6;  // 6 * 50ns = 300ns
static const uint16_t T0L = 14; // 14 * 50ns = 700ns -> total 1.0us
static const uint16_t T1H = 14; // 700ns
static const uint16_t T1L = 6;  // 300ns -> total 1.0us
static const uint32_t TRESET_TICKS = 1200; // 60us reset (60us / 50ns = 1200)

static void ws2812_init()
{
    rmt_config_t cfg = {};
    cfg.rmt_mode = RMT_MODE_TX;
    cfg.channel = (rmt_channel_t)WS_RMT_CHANNEL;
    cfg.gpio_num = (gpio_num_t)RGB_LED_PIN;
    cfg.mem_block_num = 1;
    cfg.tx_config.loop_en = false;
    cfg.tx_config.carrier_en = false;
    cfg.tx_config.idle_output_en = true;
    cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    cfg.clk_div = WS_RMT_CLK_DIV;
    ESP_ERROR_CHECK(rmt_config(&cfg));
    ESP_ERROR_CHECK(rmt_driver_install(cfg.channel, 0, 0));
}

static void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    // Select byte order according to LED wiring/protocol expectation
    // Default is GRB (classic WS2812B), can be overridden via build flag
    uint8_t data[3];
#if defined(WS_ORDER_RGB)
    data[0] = r; data[1] = g; data[2] = b;
#else // default to GRB
    data[0] = g; data[1] = r; data[2] = b;
#endif
    rmt_item32_t items[24 + 1]; // 24 bits + 1 reset item
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
    // Reset pulse (low for >= 50us)
    items[idx].duration0 = TRESET_TICKS;
    items[idx].level0 = 0;
    items[idx].duration1 = 0;
    items[idx].level1 = 0;

    ESP_ERROR_CHECK(rmt_write_items((rmt_channel_t)WS_RMT_CHANNEL, items, idx + 1, true));
    ESP_ERROR_CHECK(rmt_wait_tx_done((rmt_channel_t)WS_RMT_CHANNEL, pdMS_TO_TICKS(50)));
}
#endif // RGB_LED_PIN

static int format_slcan_standard(char* out, size_t out_sz, const twai_message_t& msg)
{
    if (!out || msg.extd) return -1;
    size_t pos = 0;
    out[pos++] = 't';

    uint16_t id = msg.identifier & 0x7FF;
    out[pos++] = nibble_to_hex((id >> 8) & 0xF);
    out[pos++] = nibble_to_hex((id >> 4) & 0xF);
    out[pos++] = nibble_to_hex(id & 0xF);

    uint8_t dlc = msg.data_length_code & 0xF;
    out[pos++] = '0' + dlc;

    for (uint8_t i = 0; i < dlc; i++)
    {
        out[pos++] = nibble_to_hex(msg.data[i] >> 4);
        out[pos++] = nibble_to_hex(msg.data[i] & 0xF);
    }

    out[pos++] = '\r';
    if (pos < out_sz) out[pos] = '\0';
    return pos;
}

static esp_err_t init_twai()
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TWAI_TX_GPIO, (gpio_num_t)TWAI_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING;
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_LOGI(TAG, "Installing TWAI driver...");
    ESP_LOGI(TAG, "Configured TWAI pins: TX=%d, RX=%d", TWAI_TX_GPIO, TWAI_RX_GPIO);
    esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret != ESP_OK) return ret;

    return twai_start();
}

static void init_tinyusb()
{
    // Use default descriptors via Kconfig
    tinyusb_config_t tusb_cfg = {};
    // Explicitly configure the TinyUSB task to avoid zero-size default
    tusb_cfg.port = TINYUSB_PORT_FULL_SPEED_0;
    tusb_cfg.task.size = 4096;           // reasonable default stack size
    tusb_cfg.task.priority = 5;          // medium priority
    // On ESP-IDF 5.5 esp_tinyusb validates affinity must be <= SOC_CPU_CORES_NUM
    // tskNO_AFFINITY (-1) is rejected, so pin to a valid core (0)
    tusb_cfg.task.xCoreID = 0; // pin TinyUSB task to CPU0
    ESP_LOGI(TAG, "Initializing TinyUSB stack...");
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "TinyUSB driver installed");

    // Initialize CDC-ACM interface 0 with no callbacks
    tinyusb_config_cdcacm_t cdc_cfg = {};
    cdc_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    cdc_cfg.callback_rx = nullptr;
    cdc_cfg.callback_rx_wanted_char = nullptr;
    cdc_cfg.callback_line_state_changed = nullptr;
    cdc_cfg.callback_line_coding_changed = nullptr;
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&cdc_cfg));
    ESP_LOGI(TAG, "TinyUSB CDC-ACM initialized");
}

[[noreturn]] static void slcan_task(void* arg)
{
    twai_message_t msg;
    char buf[SLCAN_MAX_FRAME_LEN];
    bool prev_connected = false;

    while (true)
    {
        bool now_connected = tud_cdc_connected();
        if (now_connected != prev_connected)
        {
            ESP_LOGI(TAG, "CDC connected: %s", now_connected ? "yes" : "no");
            prev_connected = now_connected;
        }
        esp_err_t r = twai_receive(&msg, pdMS_TO_TICKS(1000));
        if (r == ESP_OK && !msg.extd)
        {
            // Apply software whitelist filter for standard IDs
            uint16_t sid = msg.identifier & 0x7FF;
            // The IGNORE_WHITELIST switch is handled here for visibility
#ifndef IGNORE_WHITELIST
            if (!is_whitelisted_id(sid)) {
                continue; // drop non-whitelisted IDs
            }
#else
            // Whitelist disabled at build time for testing
            (void)sid;
#endif

            int len = format_slcan_standard(buf, sizeof(buf), msg);
            if (len > 0 && now_connected)
            {
                tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, reinterpret_cast<const uint8_t*>(buf), len);
                tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
            }
        }
    }
}

extern "C" void app_main()
{
    // Informative notice when building with whitelist disabled for testing
#ifdef IGNORE_WHITELIST
    ESP_LOGW(TAG, "IGNORE_WHITELIST is defined: forwarding ALL standard CAN frames (no filtering)");
#endif
    init_tinyusb();
#ifdef RGB_LED_PIN
    // Initialize and set onboard RGB LED to green on supported boards
    ws2812_init();
#endif
    if (init_twai() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init TWAI");
        return;
    }
    xTaskCreate(slcan_task, "slcan_task", 4096, nullptr, 5, nullptr);
    ESP_LOGI(TAG, "SLCAN bridge running");
#ifdef RGB_LED_PIN
    ws2812_set_color(0, 255, 0);
#endif
}
