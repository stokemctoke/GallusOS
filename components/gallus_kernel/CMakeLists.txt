idf_component_register(
    SRCS
        "src/error.cpp"
        "src/log.cpp"
        "src/event_bus.cpp"
        "src/scheduler.cpp"
        "src/kernel.cpp"
    INCLUDE_DIRS include
    REQUIRES esp_timer
)
