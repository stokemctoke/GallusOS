#pragma once

#include <cstddef>
#include <cstdint>

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/services/config_service.hpp"
#include "gallus/services/rest_service.hpp"

#ifndef GALLUS_HOST
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

/// @file wifi_service.hpp
/// @brief WiFi connectivity and first-boot provisioning.
///
/// Normal path: connect as a station using credentials from config
/// namespace "wifi" (keys: ssid, password). Publishes WiFiConnected
/// (with the IP) and WiFiDisconnected.
///
/// Provisioning path: when no SSID is configured, or the connection
/// fails kMaxRetries times, the service starts an open SoftAP
/// ("GallusOS-XXXX") with a captive portal: a hijack DNS server that
/// answers every query with 192.168.42.1 plus a credentials form
/// served through RestService. Saving the form persists the
/// credentials and reboots into the normal path.
///
/// STA mode uses DHCP by default. Set network/use_static_ip to true
/// (with network/ip, gateway, netmask) for the Gallus 192.168.42.x
/// subnet; static addressing on a foreign LAN will break connectivity.

namespace gallus::services {

class WifiService {
public:
    /// Payload carried by EventId::WiFiConnected.
    struct ConnectedPayload {
        uint8_t ip[4];
    };

    static constexpr int kMaxRetries = 5;

    WifiService(ConfigService& config, EventBus& events, RestService& rest)
        : config_(config), events_(events), rest_(rest) {}
    WifiService(const WifiService&) = delete;
    WifiService& operator=(const WifiService&) = delete;

    /// Bring up NVS, esp_netif and the WiFi driver. Does not connect.
    Status init();

    /// Connect as STA, or enter provisioning when unconfigured.
    Status start();

    /// Stop the radio to save power (charge mode). Publishes WiFiDisconnected.
    Status stopRadio();

    /// Reconnect STA using saved credentials after stopRadio().
    Status resumeSta();

    /// Disconnect and reconnect STA using credentials from config
    /// (after a WiFi namespace update). Not available in provisioning or
    /// charge mode.
    Status reconnectSta();

    /// WiFi band filter for scan().
    enum class ScanBand : uint8_t {
        Band2G = 0,
        Band5G = 1,
        Both = 2,
    };

    /// One AP entry from a scan. Enum fields hold the raw ESP-IDF
    /// values (wifi_auth_mode_t, wifi_cipher_type_t, …) cast to
    /// uint8_t; the REST layer maps them to human-readable strings.
    struct ApRecord {
        char ssid[33];
        uint8_t bssid[6];
        int8_t rssi;
        uint8_t channel;
        uint8_t band_ghz;         ///< 2 or 5
        uint8_t authmode;         ///< wifi_auth_mode_t
        uint8_t pairwise_cipher;  ///< wifi_cipher_type_t
        uint8_t group_cipher;     ///< wifi_cipher_type_t
        uint8_t second;           ///< wifi_second_chan_t (0 none,1 above,2 below)
        uint8_t bandwidth;        ///< wifi_bandwidth_t
        uint8_t phy_flags;        ///< bit0 b,1 g,2 n,3 a,4 ac,5 ax,6 lr
        uint8_t caps;             ///< bit0 wps,1 ftm_responder,2 ftm_initiator
        char country[4];          ///< regulatory code, e.g. "US" ("" if none)
    };

    // Bit positions for ApRecord::phy_flags.
    static constexpr uint8_t kPhy11b = 1 << 0;
    static constexpr uint8_t kPhy11g = 1 << 1;
    static constexpr uint8_t kPhy11n = 1 << 2;
    static constexpr uint8_t kPhy11a = 1 << 3;
    static constexpr uint8_t kPhy11ac = 1 << 4;
    static constexpr uint8_t kPhy11ax = 1 << 5;
    static constexpr uint8_t kPhyLr = 1 << 6;

    // Bit positions for ApRecord::caps.
    static constexpr uint8_t kCapWps = 1 << 0;
    static constexpr uint8_t kCapFtmResponder = 1 << 1;
    static constexpr uint8_t kCapFtmInitiator = 1 << 2;

    static constexpr size_t kMaxScanResults = 32;

    /// Blocking scan. Writes up to @p max records to @p out and returns
    /// the count. Requires the radio up and not in provisioning mode.
    Result<size_t> scan(ApRecord* out, size_t max,
                        ScanBand bands = ScanBand::Both);

    [[nodiscard]] bool provisioning() const { return provisioning_; }

#ifndef GALLUS_HOST
private:
    static void wifiEventHandler(void* arg, esp_event_base_t base,
                                 int32_t id, void* data);
    static void ipEventHandler(void* arg, esp_event_base_t base, int32_t id,
                               void* data);

    Status startSta(const char* ssid, const char* password);
    Status startProvisioning();
    void applyStaticIp();
    void configureApNetif();

    static esp_err_t portalGetHandler(httpd_req_t* req);
    static esp_err_t portalPostHandler(httpd_req_t* req);

    static void dnsTaskEntry(void* arg);
    void dnsLoop();

    esp_netif_t* sta_netif_ = nullptr;
    esp_netif_t* ap_netif_ = nullptr;
    TaskHandle_t dns_task_ = nullptr;
    int dns_sock_ = -1;
    int retry_count_ = 0;
#endif

    ConfigService& config_;
    EventBus& events_;
    RestService& rest_;
    bool provisioning_ = false;
    bool radio_stopped_ = false;
    bool sta_connected_ = false;
    bool initialized_ = false;
};

}  // namespace gallus::services
