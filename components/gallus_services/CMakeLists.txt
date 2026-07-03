# Gzip the dashboard at configure time and embed it in the firmware.
set(WEBUI_SRC "${CMAKE_CURRENT_LIST_DIR}/../../web/dashboard/index.html")
set(WEBUI_GZ "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
set(FAVICON_PNG "${CMAKE_CURRENT_LIST_DIR}/../../web/dashboard/favicon.png")
set(FAVICON_ICO "${CMAKE_CURRENT_LIST_DIR}/../../web/dashboard/favicon.ico")
if(EXISTS "${WEBUI_SRC}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${WEBUI_SRC}")
    execute_process(
        COMMAND gzip -9 -c "${WEBUI_SRC}"
        OUTPUT_FILE "${WEBUI_GZ}"
        RESULT_VARIABLE WEBUI_GZ_RESULT
    )
    if(NOT WEBUI_GZ_RESULT EQUAL 0)
        message(FATAL_ERROR "failed to gzip dashboard ${WEBUI_SRC}")
    endif()
else()
    message(FATAL_ERROR "dashboard source missing: ${WEBUI_SRC}")
endif()
if(NOT EXISTS "${FAVICON_PNG}")
    message(FATAL_ERROR "favicon missing: ${FAVICON_PNG}")
endif()
if(NOT EXISTS "${FAVICON_ICO}")
    message(FATAL_ERROR "favicon missing: ${FAVICON_ICO}")
endif()

idf_component_register(
    SRCS
        "src/storage_service.cpp"
        "src/config_service.cpp"
        "src/gpio_service.cpp"
        "src/rest_service.cpp"
        "src/wifi_service.cpp"
        "src/network_service.cpp"
        "src/time_service.cpp"
        "src/display_service.cpp"
        "src/battery_service.cpp"
        "src/ota_service.cpp"
        "src/webui_service.cpp"
        "src/i2c_service.cpp"
        "src/diagnostics_service.cpp"
    INCLUDE_DIRS include
    REQUIRES gallus_kernel gallus_drivers gallus_hal esp_http_server esp_netif esp_event
    PRIV_REQUIRES json esp_wifi nvs_flash lwip app_update
    EMBED_FILES "${WEBUI_GZ}" "${FAVICON_PNG}" "${FAVICON_ICO}"
)
