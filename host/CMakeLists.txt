cmake_minimum_required(VERSION 3.16)
project(gallus_host_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GALLUS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(HELLO_MANIFEST ${GENERATED_DIR}/hello_world_manifest.gen.cpp)
add_custom_command(
    OUTPUT ${HELLO_MANIFEST}
    COMMAND python3 ${GALLUS_ROOT}/tools/gallus_manifest_gen.py
            --manifest ${GALLUS_ROOT}/modules/hello_world/manifest.json
            --component hello_world
            --output ${HELLO_MANIFEST}
    DEPENDS ${GALLUS_ROOT}/modules/hello_world/manifest.json
            ${GALLUS_ROOT}/tools/gallus_manifest_gen.py
    VERBATIM
)

set(HOST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
    ${GALLUS_ROOT}/components/gallus_kernel/include
    ${GALLUS_ROOT}/components/gallus_services/include
    ${GALLUS_ROOT}/components/gallus_sdk/include
    ${GALLUS_ROOT}/components/gallus_hal/include
    ${GALLUS_ROOT}/components/gallus_drivers/include
)

add_library(gallus_host_shim STATIC
    shim/freertos_shim.cpp
    shim/esp_timer_shim.cpp
    shim/esp_log_shim.cpp
)
target_include_directories(gallus_host_shim PUBLIC ${HOST_INCLUDES})
target_compile_definitions(gallus_host_shim PUBLIC GALLUS_HOST=1)
target_link_libraries(gallus_host_shim PUBLIC pthread)

add_library(gallus_host_kernel STATIC
    ${GALLUS_ROOT}/components/gallus_kernel/src/error.cpp
    ${GALLUS_ROOT}/components/gallus_kernel/src/log.cpp
    ${GALLUS_ROOT}/components/gallus_kernel/src/event_bus.cpp
    ${GALLUS_ROOT}/components/gallus_kernel/src/scheduler.cpp
    ${GALLUS_ROOT}/components/gallus_kernel/src/kernel.cpp
)
target_link_libraries(gallus_host_kernel PUBLIC gallus_host_shim)

add_library(gallus_host_cjson STATIC
    third_party/cJSON/cJSON.c
)
target_include_directories(gallus_host_cjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON)

add_library(gallus_host_services STATIC
    mock/storage_service.cpp
    mock/rest_service.cpp
    mock/i2c_service.cpp
    mock/i2c_bus.cpp
    mock/gpio.cpp
    ${GALLUS_ROOT}/components/gallus_services/src/config_service.cpp
    ${GALLUS_ROOT}/components/gallus_services/src/gpio_service.cpp
)
target_link_libraries(gallus_host_services PUBLIC gallus_host_kernel
                                                 gallus_host_cjson)

add_library(gallus_host_sdk STATIC
    ${GALLUS_ROOT}/components/gallus_sdk/src/module_registry.cpp
    ${GALLUS_ROOT}/components/gallus_sdk/src/module_manager.cpp
)
target_link_libraries(gallus_host_sdk PUBLIC gallus_host_services)

add_library(gallus_host_modules STATIC
    ${HELLO_MANIFEST}
    ${GALLUS_ROOT}/modules/hello_world/module.cpp
)
target_link_libraries(gallus_host_modules PUBLIC gallus_host_sdk)

add_executable(gallus_host_sim main.cpp)
target_link_libraries(gallus_host_sim PRIVATE
    -Wl,--whole-archive gallus_host_modules -Wl,--no-whole-archive)

enable_testing()
add_test(NAME gallus_host_sim COMMAND gallus_host_sim)
