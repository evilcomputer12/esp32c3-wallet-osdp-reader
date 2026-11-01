idf_component_register(SRCS "main.c" "atecc.c" "reader_config.c" "feedback.c"
    "pn532.c" "nfc_reader.c" "osdp_reader.c" "wallet_reader.c" "wallet_crypto.c" "crypto_selftest.c"
    INCLUDE_DIRS "../include" "../config"
    REQUIRES reader_deps esp_timer esp_driver_uart esp_driver_i2c esp_driver_gpio
        esp_driver_usb_serial_jtag esp_hw_support spi_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
