# CAN-to-SLCAN with Filter

An ESP-IDF application for ESP32 that bridges TWAI (CAN) to SLCAN over USB CDC-ACM. It forwards only standard 11‑bit  \
CAN frames whose identifiers are in the XCSoar whitelist. For testing, you can bypass the whitelist at build time.

## Features
- TWAI (ESP32 CAN) → SLCAN bridge over USB CDC-ACM
- Software whitelist for 11‑bit standard identifiers (IDs used by XCSoar)
- Optional testing mode to forward all standard frames (`IGNORE_WHITELIST`)
- Optional BLE UART service (HM‑10 style) to stream SLCAN lines over BLE notifications

## Hardware
- Default pins: `TWAI_TX_GPIO = 18`, `TWAI_RX_GPIO = 17`
- Bitrate: 500 kbit/s (changeable in code via `TWAI_TIMING_CONFIG_500KBITS()`)

## Build
### PlatformIO
Recommended: use one of the provided environments in `platformio.ini`.

- Environment `esp32-s3-slcan`
  - Board: `esp32-s3-devkitc-1`
  - TWAI pins: `TX=18`, `RX=17`
  - Build: `pio run -e esp32-s3-slcan`
  - Flash: `pio run -t upload -e esp32-s3-slcan`

- Environment `esp32-s3-zero`
  - Board: `esp32-s3-fh4r2` (custom board JSON in `boards/`)
  - TWAI pins: `TX=4`, `RX=5`
  - Build: `pio run -e esp32-s3-zero`
  - Flash: `pio run -t upload -e esp32-s3-zero`

#### Enable BLE UART (optional)
- `env:esp32-s3-zero` already enables BLE via `-DENABLE_BLE` in `platformio.ini`.
- To enable BLE for another environment, add to that env’s `build_flags`:
  - `-DENABLE_BLE`
- BLE/NimBLE defaults are provided in `sdkconfig.defaults` and applied automatically:
  - `CONFIG_BT_ENABLED=y`
  - `CONFIG_BT_BLE_ENABLED=y`
  - `CONFIG_BT_NIMBLE_ENABLED=y`
  - `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y`
  - `CONFIG_BT_NIMBLE_SVC_GAP=y`
  - `CONFIG_BT_NIMBLE_SVC_GATT=y`
  - `CONFIG_BT_BLUEDROID_ENABLED=n`

Notes:
- `boards_dir = boards` is set in `platformio.ini`, so the custom board definition in `boards/esp32-s3-fh4r2.json` is automatically discovered.
- The serial monitor is configured for 115200 baud with RTS/DTR disabled to avoid unintended resets on the USB CDC-ACM port. You can use: `pio device monitor`.

Add the following to your environment if you want to bypass the whitelist for testing:

```
build_flags = -DIGNORE_WHITELIST
```

### ESP-IDF (CMake)
- Component level (in `src/CMakeLists.txt` after `idf_component_register`):

```
target_compile_definitions(${COMPONENT_LIB} PRIVATE IGNORE_WHITELIST)
```

- Project level (top-level `CMakeLists.txt`):

```
add_compile_definitions(IGNORE_WHITELIST)
```

## Runtime behavior
- When `IGNORE_WHITELIST` is defined, the app logs a warning at startup and forwards all standard (11‑bit) CAN frames without filtering.
- Otherwise, only frames with whitelisted IDs are formatted and sent over CDC as SLCAN lines.
- When BLE is enabled and a central subscribes to notifications, SLCAN lines are also sent over BLE.

### BLE UART details
- Device name: `SLCAN-<addr>-LE` (where `<addr>` are the lower 3 bytes of the BLE MAC in lowercase hex).
- Advertising:
  - Primary ADV includes Flags, Complete Local Name, and 16‑bit Service UUID list with GAP (`0x1800`).
  - Scan Response includes the 128‑bit UART Service UUID `0000ffe0-0000-1000-8000-00805f9b34fb`.
  - Connectable undirected; interval ~152–200 ms.
- GATT:
  - Service: `FFE0` (`0000ffe0-0000-1000-8000-00805f9b34fb`).
  - Characteristic: `FFE1` (`0000ffe1-0000-1000-8000-00805f9b34fb`) with READ | WRITE | WRITE_NO_RSP | NOTIFY (CCCD present).
- Data flow:
  - USB CDC continues to operate as before.
  - BLE notifications are sent only when a client is connected and has enabled notifications.

### BLE troubleshooting
- If the device isn’t visible in some Android apps:
  - Try nRF Connect or LightBlue; scan for at least 30–60 s and stay within ~0.5 m.
  - Ensure Bluetooth is enabled and no strict filters are applied in the app.
- If notifications fail to enable:
  - Ensure you enable notifications on characteristic `FFE1` within service `FFE0`.
  - Power‑cycle the board and retry.

## Notes
- Extended (29‑bit) CAN frames are ignored.
- The whitelist is defined in `src/whitelist.h` and enforced in `src/whitelist.cpp`; the bypass switch is applied in `src/main.cpp`.
 - BLE is optional; without `-DENABLE_BLE` the BLE module compiles to no‑ops and USB behavior is unchanged.
