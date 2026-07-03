cmake_minimum_required(VERSION 3.16)

# Modules are IDF components discovered from /modules (Phase 4+).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/modules")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gallus_os)
