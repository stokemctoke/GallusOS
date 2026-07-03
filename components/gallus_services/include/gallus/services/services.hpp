#pragma once

/// @file services.hpp
/// @brief Core services (Phase 2+): Config, Storage, GPIO, WiFi,
/// Network, Time, OTA, Display, Battery, WebUI, REST, Module,
/// Diagnostics.
///
/// Each service exposes a clean interface. Modules consume services
/// and kernel primitives only — never drivers or hardware.

namespace gallus::services {}
