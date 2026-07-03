# GallusOS module build glue.
#
# A module's CMakeLists.txt is two lines:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../../tools/gallus_module.cmake)
#   gallus_module(SRCS module.cpp)
#
# The macro validates manifest.json, generates the registration code,
# and registers the module as an IDF component linked WHOLE_ARCHIVE so
# its static registrar survives the linker.
#
# NOTE: IDF processes component CMakeLists twice (an early
# "requirements expansion" pass, then the real configure pass). The
# codegen only runs in the real pass; paths use CMAKE_CURRENT_LIST_DIR
# which is correct in both.

set(GALLUS_MANIFEST_GEN "${CMAKE_CURRENT_LIST_DIR}/gallus_manifest_gen.py")

macro(gallus_module)
    cmake_parse_arguments(GM "" "" "SRCS" ${ARGN})

    get_filename_component(GM_NAME "${CMAKE_CURRENT_LIST_DIR}" NAME)
    set(GM_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/manifest.json")
    set(GM_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/${GM_NAME}_manifest.gen.cpp")

    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        if(NOT EXISTS "${GM_MANIFEST}")
            message(FATAL_ERROR
                "GallusOS module '${GM_NAME}' has no manifest.json")
        endif()

        # Re-run CMake (and the generator) when the manifest changes.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     "${GM_MANIFEST}")

        idf_build_get_property(GM_PYTHON PYTHON)
        if(NOT GM_PYTHON)
            set(GM_PYTHON python3)
        endif()

        execute_process(
            COMMAND "${GM_PYTHON}" "${GALLUS_MANIFEST_GEN}"
                    --manifest "${GM_MANIFEST}"
                    --component "${GM_NAME}"
                    --output "${GM_GENERATED}"
            RESULT_VARIABLE GM_RESULT
            ERROR_VARIABLE GM_ERROR
        )
        if(NOT GM_RESULT EQUAL 0)
            message(FATAL_ERROR
                "GallusOS manifest validation failed for module "
                "'${GM_NAME}':\n${GM_ERROR}")
        endif()
    endif()

    idf_component_register(
        SRCS ${GM_SRCS} "${GM_GENERATED}"
        INCLUDE_DIRS "."
        REQUIRES gallus_sdk
        WHOLE_ARCHIVE
    )
endmacro()
