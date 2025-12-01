# CAN-to-SLCAN with Filter (ESP-IDF)

An ESP-IDF application for ESP32 that bridges TWAI (CAN) to SLCAN over USB CDC-ACM. It forwards only standard 11‑bit CAN frames whose identifiers are in the XCSoar whitelist. For testing, you can bypass the whitelist at build time.

## Features
- TWAI (ESP32 CAN) → SLCAN bridge over USB CDC-ACM
- Software whitelist for 11‑bit standard identifiers (XCSoar telemetry IDs)
- Optional testing mode to forward all standard frames (`IGNORE_WHITELIST`)

## Hardware
- Default pins: `TWAI_TX_GPIO = 18`, `TWAI_RX_GPIO = 17`
- Bitrate: 500 kbit/s (changeable in code via `TWAI_TIMING_CONFIG_500KBITS()`)

## Build
### PlatformIO
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

## Notes
- Extended (29‑bit) CAN frames are ignored.
- The whitelist is defined in `src/whitelist.h` and enforced in `src/whitelist.cpp`; the bypass switch is applied in `src/main.cpp`.
