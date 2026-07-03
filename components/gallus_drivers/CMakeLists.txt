idf_component_register(
    SRCS
        "src/gpio.cpp"
        "src/i2c_bus.cpp"
        "src/ssd1306.cpp"
        "src/adc.cpp"
    INCLUDE_DIRS include
    REQUIRES gallus_kernel gallus_hal esp_driver_i2c
    PRIV_REQUIRES esp_driver_gpio esp_adc
)
