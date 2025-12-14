// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

// Optional BLE UART (Nordic UART Service-like) interface.
// This module compiles to no-ops unless ENABLE_BLE is defined via build flags.

// Initialize BLE UART service and start advertising (if ENABLE_BLE).
void ble_init();

// Returns true if there is an active BLE UART connection (if ENABLE_BLE),
// otherwise always false.
bool ble_uart_connected();

// Send bytes over BLE UART TX as notification (if ENABLE_BLE).
// Returns number of bytes queued/sent, or 0 on failure or when BLE disabled.
size_t ble_uart_write(const uint8_t* data, size_t len);
