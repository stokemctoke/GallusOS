#pragma once

/// @file version.hpp
/// @brief GallusOS version numbers (see VERSIONING in the spec).

#define GALLUS_VERSION_MAJOR 0
#define GALLUS_VERSION_MINOR 1
#define GALLUS_VERSION_PATCH 3
#define GALLUS_VERSION_STRING "0.1.3"

namespace gallus {

/// Firmware version string ("major.minor.patch").
constexpr const char* kVersion = GALLUS_VERSION_STRING;

}  // namespace gallus
