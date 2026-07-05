idf_component_register(
    SRCS
        "main.cpp"
        "api_routes.cpp"
    REQUIRES gallus_kernel gallus_hal gallus_drivers gallus_services gallus_sdk
    PRIV_REQUIRES json esp_timer esp_wifi
)
