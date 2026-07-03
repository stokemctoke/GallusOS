idf_component_register(
    SRCS
        "src/module_registry.cpp"
        "src/module_manager.cpp"
    INCLUDE_DIRS include
    REQUIRES gallus_kernel gallus_services
)
