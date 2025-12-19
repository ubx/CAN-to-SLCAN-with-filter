// SPDX-License-Identifier: GPL-3.0-only
#include "ble.h"

#ifndef ENABLE_BLE

// No-op implementations when BLE is disabled
void ble_init()
{
}
bool ble_uart_connected() { return false; }
size_t ble_uart_write(const uint8_t* /*data*/, size_t /*len*/) { return 0; }

#else

#include <cstring>
#include <cstdio>
#include "esp_log.h"

// NimBLE (ESP-IDF)
// Support both include layouts (IDF component vs upstream layout)
#if __has_include("esp_nimble_hci.h")
#include "esp_nimble_hci.h"
#elif __has_include("nimble/esp_port/esp_nimble_hci.h")
#include "nimble/esp_port/esp_nimble_hci.h"
#else
#error "esp_nimble_hci.h not found; ensure esp_nimble component is added and Bluetooth is enabled in sdkconfig"
#endif
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_hs_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// UART service UUIDs per request (TI/Legacy FFE0/FFE1 style)
// 16-bit aliases:
//   Service: 0xFFE0
//   Characteristic: 0xFFE1
// 128-bit forms:
//   Service:       0000ffe0-0000-1000-8000-00805f9b34fb
//   Characteristic:0000ffe1-0000-1000-8000-00805f9b34fb

static const char* TAG = "ble_uart";

static uint16_t s_conn_handle = 0xFFFF;
static uint16_t s_tx_val_handle = 0; // attribute handle for TX characteristic value
static bool s_tx_notify_enabled = false;
// Dynamic device name buffer, updated after BLE address is known
static char s_devname[32] = "SLCAN-000000-LE";

// UUID helpers
// NimBLE expects little-endian byte order in BLE_UUID128_INIT
// UUID: 0000ffe0-0000-1000-8000-00805f9b34fb
static const ble_uuid128_t UUID_UART_SERVICE = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00);
// Data UUID (HM-10 style, single characteristic for RX/TX): 0000ffe1-0000-1000-8000-00805f9b34fb
static const ble_uuid128_t UUID_UART_CHAR_FFE1 = BLE_UUID128_INIT(
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE1, 0xFF, 0x00, 0x00);

static int gatt_rw_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        // Received data from central to RX characteristic; ignore contents for now.
        ESP_LOGD(TAG, "RX write, len=%d", (int)OS_MBUF_PKTLEN(ctxt->om));
        // Could parse here if needed.
        return 0;
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        // Optional: allow reading current (empty) value
        const char* empty = "";
        int rc = os_mbuf_append(ctxt->om, empty, 0);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Use mutable defs to avoid C++ designated initializer constraints
static struct ble_gatt_chr_def gatt_nus_chars[] = {{}, {}};
static struct ble_gatt_svc_def gatt_svcs[] = {{}, {}};

static void ble_advertise();
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static int gap_event(struct ble_gap_event* event, void* /*arg*/)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, handle=%u", s_conn_handle);
        }
        else
        {
            ESP_LOGW(TAG, "Connect failed; status=%d", event->connect.status);
            s_conn_handle = 0xFFFF;
            ble_advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        s_conn_handle = 0xFFFF;
        s_tx_notify_enabled = false;
        ble_advertise();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_val_handle)
        {
            s_tx_notify_enabled = event->subscribe.cur_notify || event->subscribe.cur_indicate;
            ESP_LOGI(TAG, "TX notify %s (BLE_GATT_CHR_F_NOTIFY)", s_tx_notify_enabled ? "enabled" : "disabled");
        }
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: %u", event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

static void ble_advertise()
{
    struct ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND; // connectable undirected
    params.disc_mode = BLE_GAP_DISC_MODE_GEN; // general discoverable
    // Keep interval moderate to help scanners catch ADV+SR pair (152–200 ms)
    params.itvl_min = 0x00F8; // 152.5 ms
    params.itvl_max = 0x0140; // 200 ms

    // Compose ADV with compatibility hints: Flags + Name + Tx Power + Appearance + 16-bit UUID (GAP 0x1800).
    // If it doesn't fit, fall back to shortened name automatically.
    struct ble_hs_adv_fields adv = {};
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char* adv_name = s_devname;
    adv.name = (uint8_t*)adv_name;
    adv.name_len = (uint8_t)strlen(adv_name);
    adv.name_is_complete = 1;
    // TX Power present (0 dBm as a neutral value)
    adv.tx_pwr_lvl_is_present = 1;
    adv.tx_pwr_lvl = 0;
    // Appearance: 0 (unspecified) to keep size small
    adv.appearance = 0;
    adv.appearance_is_present = 1;
    // 16-bit UUID in ADV: GAP (0x1800)
    static ble_uuid16_t uu16_gap;
    uu16_gap = BLE_UUID16_INIT(0x1800);
    adv.uuids16 = &uu16_gap;
    adv.num_uuids16 = 1;
    // Mark as complete list to satisfy apps that look for explicit GAP UUID presence
    adv.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "adv_set_fields rc=%d, trying shortened name fallback", rc);
        // Fallback: shorten the local name to fit
        struct ble_hs_adv_fields adv_fallback = {};
        adv_fallback.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        adv_fallback.tx_pwr_lvl_is_present = 1;
        adv_fallback.tx_pwr_lvl = 0;
        adv_fallback.appearance = 0;
        adv_fallback.appearance_is_present = 1;
        adv_fallback.uuids16 = &uu16_gap;
        adv_fallback.num_uuids16 = 1;
        adv_fallback.uuids16_is_complete = 1;
        // shorten name to first 12 chars (arbitrary small) if too long
        char short_name[16];
        size_t nlen = strlen(s_devname);
        size_t keep = nlen < 12 ? nlen : 12;
        memcpy(short_name, s_devname, keep);
        short_name[keep] = '\0';
        adv_fallback.name = (uint8_t*)short_name;
        adv_fallback.name_len = (uint8_t)strlen(short_name);
        adv_fallback.name_is_complete = 0; // shortened
        rc = ble_gap_adv_set_fields(&adv_fallback);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "adv_set_fields (fallback) rc=%d", rc);
            return;
        }
    }

    // Scan Response: put the 128-bit UART service UUID here
    struct ble_hs_adv_fields sr = {};
    sr.num_uuids128 = 1;
    sr.uuids128 = (ble_uuid128_t*)&UUID_UART_SERVICE;
    sr.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&sr);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "adv_rsp_set_fields rc=%d; continuing without SR", rc);
        // continue without SR rather than aborting
    }

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "adv_start rc=%d (own_addr_type=%u)", rc, (unsigned)s_own_addr_type);
    }
    else
    {
        ESP_LOGI(TAG, "Advertising started (own_addr_type=%u)", (unsigned)s_own_addr_type);
    }
}

static void on_sync(void)
{
    // Determine proper own address type (public or random static)
    int rc = ble_hs_id_infer_auto(0 /*privacy*/, &s_own_addr_type);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "infer_auto failed rc=%d, retry with privacy", rc);
        rc = ble_hs_id_infer_auto(1 /*privacy*/, &s_own_addr_type);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "ble_hs_id_infer_auto (privacy) rc=%d", rc);
            return;
        }
    }

    uint8_t addr[6] = {};
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    // Update the device name to include lower 3 bytes of BLE address (lowercase hex)
    std::snprintf(s_devname, sizeof(s_devname), "SLCAN-%02x%02x%02x-LE", addr[2], addr[1], addr[0]);
    ble_svc_gap_device_name_set(s_devname);
    ESP_LOGI(TAG, "BLE own addr type=%u addr=%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)s_own_addr_type,
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    ble_advertise();
}

static void host_task(void* param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_init()
{
    // Initialize NimBLE host stack
    int nerr = nimble_port_init();
    if (nerr != 0)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", nerr);
        return;
    }

    // Set the initial device name (will be updated in on_sync() once address is known)
    ble_svc_gap_device_name_set(s_devname);

    // Register GAP and GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Single HM-10 style data characteristic on FFE1: READ | WRITE | WNR | NOTIFY
    // BLE_GATT_CHR_F_NOTIFY is the NimBLE equivalent of ESP_GATT_CHAR_PROP_BIT_NOTIFY
    // The CCCD (0x2902) is automatically added by NimBLE when NOTIFY or INDICATE flags are set
    gatt_nus_chars[0] = {};
    gatt_nus_chars[0].uuid = &UUID_UART_CHAR_FFE1.u;
    gatt_nus_chars[0].access_cb = gatt_rw_access_cb;
    gatt_nus_chars[0].arg = nullptr;
    gatt_nus_chars[0].descriptors = nullptr; // NimBLE auto-adds CCCD for notify/indicate
    gatt_nus_chars[0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
        BLE_GATT_CHR_F_NOTIFY; // <-- This is the key flag!
    gatt_nus_chars[0].val_handle = &s_tx_val_handle;

    // Terminator
    gatt_nus_chars[1] = {};

    // Fill service definitions
    gatt_svcs[0] = {};
    gatt_svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    gatt_svcs[0].includes = nullptr;
    gatt_svcs[0].uuid = &UUID_UART_SERVICE.u;
    gatt_svcs[0].characteristics = gatt_nus_chars;

    // Terminator
    gatt_svcs[1] = {};

    // Add our UART-like service
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "gatt add svcs rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "GATT UART service (FFE0) added; DATA=FFE1 (read/write/notify); tx_val_handle=%u",
             (unsigned)s_tx_val_handle);

    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task);
}

bool ble_uart_connected()
{
    return s_conn_handle != 0xFFFF && s_tx_notify_enabled;
}

size_t ble_uart_write(const uint8_t* data, size_t len)
{
    if (!data || len == 0) return 0;
    if (!ble_uart_connected()) return 0;

    // Prepare payload
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om)
    {
        // Out of mbufs - BLE stack is congested
        return 0;
    }

    // ble_gatts_notify_custom() uses the BLE_GATT_CHR_F_NOTIFY property
    // to send unacknowledged notifications to the central
    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
    if (rc != 0)
    {
        // Failed to send notification
        // Note: os_mbuf is freed automatically by notify_custom on error
        if (rc == BLE_HS_ENOMEM)
        {
            // Stack congestion - caller should retry
            ESP_LOGD(TAG, "BLE stack congested (ENOMEM)");
        }
        else
        {
            ESP_LOGW(TAG, "notify failed: rc=%d", rc);
        }
        return 0;
    }
    return len;
}

#endif // ENABLE_BLE
