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

#define TWAI_TX_GPIO ((gpio_num_t)18)
#define TWAI_RX_GPIO ((gpio_num_t)17)
#define TWAI_TIMING TWAI_TIMING_CONFIG_500KBITS()
#define SLCAN_MAX_FRAME_LEN 32

static inline char nibble_to_hex(uint8_t n)
{
    n &= 0xF;
    return (n < 10) ? ('0' + n) : ('A' + (n - 10));
}

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
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_GPIO, TWAI_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING;
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_LOGI(TAG, "Installing TWAI driver...");
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

static void slcan_task(void* arg)
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
    if (init_twai() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init TWAI");
        return;
    }
    xTaskCreate(slcan_task, "slcan_task", 4096, nullptr, 5, nullptr);
    ESP_LOGI(TAG, "SLCAN bridge running");
}
