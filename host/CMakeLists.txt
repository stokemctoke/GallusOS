cmake_minimum_required(VERSION 3.16)
project(gallus_host_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GALLUS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(gallus_host_shim STATIC
    shim/freertos_shim.cpp
    shim/esp_timer_shim.cpp
    shim/esp_log_shim.cpp
)
target_include_directories(gallus_host_shim PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
)
target_compile_definitions(gallus_host_shim PUBLIC GALLUS_HOST=1)
target_link_libraries(gallus_host_shim PUBLIC pthread)

set(GALLUS_KERNEL_SOURCES
    ${GALLUS_ROOT}/components/gallus_kernel/src/error.cpp
    ${GALLUS_ROOT}/components/gallus_kernel/src/log.cpp
    ${GALLUS_ROOT}/components/gallus_kernel/src/event_bus.cpp
    ${GALLUS_ROOT}/components/gallus_kernel/src/scheduler.cpp
    ${GALLUS_ROOT}/components/gallus_kernel/src/kernel.cpp
)

add_library(gallus_host_kernel STATIC ${GALLUS_KERNEL_SOURCES})
target_include_directories(gallus_host_kernel PUBLIC
    ${GALLUS_ROOT}/components/gallus_kernel/include
)
target_link_libraries(gallus_host_kernel PUBLIC gallus_host_shim)

add_executable(gallus_host_sim main.cpp)
target_link_libraries(gallus_host_sim PRIVATE gallus_host_kernel gallus_host_shim)

enable_testing()
add_test(NAME gallus_host_sim COMMAND gallus_host_sim)
