idf_component_register(
        SRCS
        "main.cpp"
        "whitelist.cpp"
        INCLUDE_DIRS
        "."
        REQUIRES
        driver          # TWAI
        esp_tinyusb     # ESP-IDF TinyUSB stack
        usb             # USB device driver layer
        esp_driver_usb_serial_jtag
)
