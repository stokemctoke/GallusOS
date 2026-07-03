#include "gallus/services/wifi_service.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_mac.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#include "gallus/kernel.hpp"
#include "gallus/log.hpp"
#include "gallus/error.hpp"

namespace gallus::services {

namespace {

constexpr const char* kTag = "WiFi";
constexpr uint32_t kDnsStackBytes = 3072;
constexpr UBaseType_t kDnsTaskPriority = 4;

// SoftAP / provisioning subnet (matches other Gallus apps).
constexpr uint8_t kApAddr[4] = {192, 168, 42, 1};

constexpr const char* kPortalHtml =
    "<!doctype html><html><head>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>GallusOS Setup</title><style>"
    "body{background:#0E0E12;color:#F2EDE6;font-family:sans-serif;"
    "display:flex;justify-content:center;padding-top:8vh;margin:0}"
    ".card{background:#1C1C24;padding:2em;border-radius:12px;"
    "max-width:320px;width:90%}"
    "h1{color:#E8900A;font-size:1.3em;margin-top:0}"
    "p{color:#9E9890;font-size:.85em}"
    "label{font-size:.9em}"
    "input{width:100%;padding:.7em;margin:.3em 0 1em;border-radius:6px;"
    "border:1px solid #26262F;background:#15151B;color:#F2EDE6;"
    "box-sizing:border-box}"
    "button{width:100%;padding:.8em;background:#E8900A;border:0;"
    "border-radius:6px;color:#0E0E12;font-weight:bold;font-size:1em}"
    "</style></head><body><div class=card>"
    "<h1>GallusOS WiFi Setup</h1>"
    "<p>Connect this device to your network.</p>"
    "<form method=POST action=/provision>"
    "<label>Network name (SSID)</label>"
    "<input name=ssid required maxlength=32>"
    "<label>Password</label>"
    "<input name=password type=password maxlength=64>"
    "<button>Save &amp; Reboot</button></form></div></body></html>";

constexpr const char* kPortalDoneHtml =
    "<!doctype html><html><head>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>GallusOS Setup</title><style>"
    "body{background:#0E0E12;color:#F2EDE6;font-family:sans-serif;"
    "display:flex;justify-content:center;padding-top:8vh;margin:0}"
    ".card{background:#1C1C24;padding:2em;border-radius:12px;"
    "max-width:320px;width:90%}"
    "h1{color:#E8900A;font-size:1.3em;margin-top:0}"
    "</style></head><body><div class=card>"
    "<h1>Saved</h1><p>GallusOS is rebooting and will join your "
    "network. You can close this page.</p></div></body></html>";

/// In-place decode of application/x-www-form-urlencoded values.
void urlDecode(char* text) {
    char* out = text;
    for (const char* in = text; *in != '\0'; ++in) {
        if (*in == '+') {
            *out++ = ' ';
        } else if (*in == '%' && in[1] != '\0' && in[2] != '\0') {
            char hex[3] = {in[1], in[2], '\0'};
            *out++ = static_cast<char>(strtol(hex, nullptr, 16));
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
}

void restartLater(void* /*ctx*/) { esp_restart(); }

bool parseIp4(const char* text, esp_ip4_addr_t* out) {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    if (sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return false;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    out->addr = ESP_IP4TOADDR(static_cast<uint8_t>(a), static_cast<uint8_t>(b),
                              static_cast<uint8_t>(c), static_cast<uint8_t>(d));
    return true;
}

}  // namespace

Status WifiService::init() {
    if (initialized_) {
        return Error::InvalidState;
    }

    // NVS backs the WiFi driver's calibration/config storage.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return fromEspErr(err);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif_ = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return fromEspErr(err);
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiService::wifiEventHandler, this,
        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiService::ipEventHandler, this,
        nullptr));

    // Hostname (also used by NetworkService for mDNS).
    char hostname[32];
    char fallback[32];
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(fallback, sizeof(fallback), "gallus-%02x%02x", mac[4], mac[5]);
    (void)config_.getString("network", "hostname", hostname,
                            sizeof(hostname), fallback);
    (void)esp_netif_set_hostname(sta_netif_, hostname);

    initialized_ = true;
    return Status::success();
}

Status WifiService::start() {
    if (!initialized_) {
        return Error::InvalidState;
    }

    char ssid[33] = {};
    char password[65] = {};
    (void)config_.getString("wifi", "ssid", ssid, sizeof(ssid), "");
    (void)config_.getString("wifi", "password", password, sizeof(password),
                            "");

    if (ssid[0] == '\0') {
        Log::warn(kTag, "no credentials configured — entering provisioning");
        return startProvisioning();
    }
    return startSta(ssid, password);
}

Status WifiService::stopRadio() {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (provisioning_) {
        return Error::InvalidState;
    }

    radio_stopped_ = true;
    sta_connected_ = false;
    const esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        return fromEspErr(err);
    }
    (void)events_.publish(Event::make(EventId::WiFiDisconnected));
    Log::info(kTag, "radio stopped");
    return Status::success();
}

Status WifiService::resumeSta() {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (provisioning_) {
        return Error::InvalidState;
    }

    char ssid[33] = {};
    char password[65] = {};
    (void)config_.getString("wifi", "ssid", ssid, sizeof(ssid), "");
    (void)config_.getString("wifi", "password", password, sizeof(password),
                            "");
    if (ssid[0] == '\0') {
        return Error::InvalidState;
    }

    retry_count_ = 0;
    radio_stopped_ = false;
    return startSta(ssid, password);
}

Status WifiService::reconnectSta() {
    if (!initialized_ || provisioning_ || radio_stopped_) {
        return Error::InvalidState;
    }

    char ssid[33] = {};
    char password[65] = {};
    (void)config_.getString("wifi", "ssid", ssid, sizeof(ssid), "");
    (void)config_.getString("wifi", "password", password, sizeof(password),
                            "");
    if (ssid[0] == '\0') {
        return Error::InvalidArg;
    }

    retry_count_ = 0;
    (void)esp_wifi_disconnect();
    (void)esp_wifi_stop();
    Log::info(kTag, "reconnecting to '%s'...", ssid);
    return startSta(ssid, password);
}

namespace {

bool channelIs5G(uint8_t channel) { return channel >= 36; }

bool bssidEqual(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 6; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

bool alreadyListed(const WifiService::ApRecord* out, size_t count,
                   const uint8_t* bssid) {
    for (size_t i = 0; i < count; ++i) {
        if (bssidEqual(out[i].bssid, bssid)) {
            return true;
        }
    }
    return false;
}

void copyApRecord(WifiService::ApRecord* dst, const wifi_ap_record_t& src,
                  uint8_t band_ghz) {
    std::memcpy(dst->ssid, src.ssid, sizeof(dst->ssid));
    std::memcpy(dst->bssid, src.bssid, sizeof(dst->bssid));
    dst->rssi = src.rssi;
    dst->channel = src.primary;
    dst->band_ghz = band_ghz != 0 ? band_ghz
                                  : (channelIs5G(src.primary) ? 5 : 2);
}

enum class BandFilter : uint8_t {
    All = 0,
    Only2G,
    Only5G,
};

bool bandMatches(BandFilter filter, uint8_t channel) {
    switch (filter) {
        case BandFilter::Only2G:
            return !channelIs5G(channel);
        case BandFilter::Only5G:
            return channelIs5G(channel);
        case BandFilter::All:
            return true;
    }
    return true;
}

Status runScan(WifiService::ApRecord* out, size_t max, size_t& total,
               BandFilter filter) {
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        return fromEspErr(err);
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        return fromEspErr(err);
    }
    if (ap_count == 0) {
        return Status::success();
    }

    if (ap_count > WifiService::kMaxScanResults) {
        ap_count = static_cast<uint16_t>(WifiService::kMaxScanResults);
    }

    wifi_ap_record_t* records = static_cast<wifi_ap_record_t*>(
        std::malloc(static_cast<size_t>(ap_count) * sizeof(wifi_ap_record_t)));
    if (records == nullptr) {
        return Error::NoMemory;
    }

    err = esp_wifi_scan_get_ap_records(&ap_count, records);
    if (err != ESP_OK) {
        std::free(records);
        return fromEspErr(err);
    }

    for (uint16_t i = 0; i < ap_count && total < max; ++i) {
        if (!bandMatches(filter, records[i].primary)) {
            continue;
        }
        if (alreadyListed(out, total, records[i].bssid)) {
            continue;
        }
        copyApRecord(&out[total], records[i], 0);
        ++total;
    }
    std::free(records);
    return Status::success();
}

}  // namespace

Result<size_t> WifiService::scan(ApRecord* out, size_t max, ScanBand bands) {
    if (!initialized_ || provisioning_ || radio_stopped_ || !sta_connected_) {
        return Error::InvalidState;
    }
    if (out == nullptr || max == 0) {
        return Error::InvalidArg;
    }

    size_t total = 0;
    BandFilter filter = BandFilter::All;
    if (bands == ScanBand::Band2G) {
        filter = BandFilter::Only2G;
    } else if (bands == ScanBand::Band5G) {
        filter = BandFilter::Only5G;
    }
    GALLUS_RETURN_IF_ERROR(runScan(out, max, total, filter));
    return total;
}

void WifiService::applyStaticIp() {
    if (!config_.getBool("network", "use_static_ip", false)) {
        Log::info(kTag, "using DHCP");
        return;
    }

    char ip_str[16] = {};
    char gw_str[16] = {};
    char mask_str[16] = {};
    (void)config_.getString("network", "ip", ip_str, sizeof(ip_str),
                            "192.168.42.42");
    (void)config_.getString("network", "gateway", gw_str, sizeof(gw_str),
                            "192.168.42.1");
    (void)config_.getString("network", "netmask", mask_str, sizeof(mask_str),
                            "255.255.255.0");

    esp_netif_ip_info_t info = {};
    if (!parseIp4(ip_str, &info.ip) || !parseIp4(gw_str, &info.gw) ||
        !parseIp4(mask_str, &info.netmask)) {
        Log::warn(kTag, "invalid static IP config — using DHCP");
        return;
    }

    (void)esp_netif_dhcpc_stop(sta_netif_);
    if (esp_netif_set_ip_info(sta_netif_, &info) != ESP_OK) {
        Log::warn(kTag, "failed to apply static IP");
        return;
    }
    Log::info(kTag, "static IP " IPSTR, IP2STR(&info.ip));
}

void WifiService::configureApNetif() {
    if (ap_netif_ == nullptr) {
        return;
    }

    esp_netif_ip_info_t info = {};
    info.ip.addr = ESP_IP4TOADDR(kApAddr[0], kApAddr[1], kApAddr[2], kApAddr[3]);
    info.gw.addr = ESP_IP4TOADDR(kApAddr[0], kApAddr[1], kApAddr[2], kApAddr[3]);
    info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);

    (void)esp_netif_dhcps_stop(ap_netif_);
    (void)esp_netif_set_ip_info(ap_netif_, &info);
    (void)esp_netif_dhcps_start(ap_netif_);
}

Status WifiService::startSta(const char* ssid, const char* password) {
    wifi_config_t sta_cfg = {};
    snprintf(reinterpret_cast<char*>(sta_cfg.sta.ssid),
             sizeof(sta_cfg.sta.ssid), "%s", ssid);
    snprintf(reinterpret_cast<char*>(sta_cfg.sta.password),
             sizeof(sta_cfg.sta.password), "%s", password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    applyStaticIp();

    const esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        return fromEspErr(err);
    }
    Log::info(kTag, "connecting to '%s'...", ssid);
    return Status::success();
}

void WifiService::wifiEventHandler(void* arg, esp_event_base_t /*base*/,
                                   int32_t id, void* /*data*/) {
    auto* self = static_cast<WifiService*>(arg);

    switch (id) {
        case WIFI_EVENT_STA_START:
            (void)esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            self->sta_connected_ = false;
            (void)self->events_.publish(
                Event::make(EventId::WiFiDisconnected));
            if (self->provisioning_ || self->radio_stopped_) {
                break;
            }
            if (self->retry_count_ < kMaxRetries) {
                self->retry_count_++;
                Log::warn(kTag, "disconnected, retry %d/%d",
                          self->retry_count_, kMaxRetries);
                (void)esp_wifi_connect();
            } else {
                Log::error(kTag,
                           "connection failed %d times — entering "
                           "provisioning",
                           kMaxRetries);
                (void)self->startProvisioning();
            }
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            (void)self->events_.publish(Event::make(EventId::ClientConnected));
            Log::info(kTag, "portal client joined");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            (void)self->events_.publish(
                Event::make(EventId::ClientDisconnected));
            break;

        default:
            break;
    }
}

void WifiService::ipEventHandler(void* arg, esp_event_base_t /*base*/,
                                 int32_t id, void* data) {
    auto* self = static_cast<WifiService*>(arg);
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    const auto* event = static_cast<ip_event_got_ip_t*>(data);
    self->retry_count_ = 0;
    self->sta_connected_ = true;

    ConnectedPayload payload = {};
    payload.ip[0] = esp_ip4_addr1(&event->ip_info.ip);
    payload.ip[1] = esp_ip4_addr2(&event->ip_info.ip);
    payload.ip[2] = esp_ip4_addr3(&event->ip_info.ip);
    payload.ip[3] = esp_ip4_addr4(&event->ip_info.ip);

    Log::info(kTag, "connected, IP " IPSTR, IP2STR(&event->ip_info.ip));
    (void)self->events_.publish(
        Event::make(EventId::WiFiConnected, payload));
}

// ---------------------------------------------------------------------------
// Provisioning
// ---------------------------------------------------------------------------

Status WifiService::startProvisioning() {
    if (provisioning_) {
        return Error::InvalidState;
    }
    provisioning_ = true;

    (void)esp_wifi_stop();

    if (ap_netif_ == nullptr) {
        ap_netif_ = esp_netif_create_default_wifi_ap();
    }
    configureApNetif();

    // Use the STA MAC so the AP name matches the mDNS hostname
    // (the SoftAP MAC is the STA MAC + 1).
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    wifi_config_t ap_cfg = {};
    snprintf(reinterpret_cast<char*>(ap_cfg.ap.ssid), sizeof(ap_cfg.ap.ssid),
             "GallusOS-%02X%02X", mac[4], mac[5]);
    ap_cfg.ap.ssid_len = strlen(reinterpret_cast<char*>(ap_cfg.ap.ssid));
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Hijack DNS so every hostname leads to the portal.
    if (dns_task_ == nullptr) {
        xTaskCreate(&WifiService::dnsTaskEntry, "gallus_dns", kDnsStackBytes,
                    this, kDnsTaskPriority, &dns_task_);
    }

    // Portal routes: unregister the dashboard root (registered earlier by
    // WebUiService) so GET / serves the setup form. Without this, Ubuntu's
    // captive check (connectivity-check.ubuntu.com → /) hits the dashboard.
    (void)rest_.unregisterRoute(HTTP_GET, "/");

    Status status = rest_.registerRoute(HTTP_POST, "/provision",
                                        &WifiService::portalPostHandler, this);
    if (status.ok()) {
        status = rest_.registerRoute(HTTP_GET, "/",
                                     &WifiService::portalGetHandler, this);
    }
    if (status.ok()) {
        status = rest_.registerRoute(HTTP_GET, "/*",
                                     &WifiService::portalGetHandler, this);
    }

    Log::info(kTag, "provisioning portal up: join '%s' and browse anywhere",
              reinterpret_cast<char*>(ap_cfg.ap.ssid));
    return status;
}

esp_err_t WifiService::portalGetHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, kPortalHtml);
}

esp_err_t WifiService::portalPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WifiService*>(req->user_ctx);

    char body[192] = {};
    const int received =
        httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[33] = {};
    char password[65] = {};
    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }
    (void)httpd_query_key_value(body, "password", password, sizeof(password));
    urlDecode(ssid);
    urlDecode(password);

    Status status = self->config_.setString("wifi", "ssid", ssid);
    if (status.ok()) {
        status = self->config_.setString("wifi", "password", password);
    }
    if (!status.ok()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "failed to persist credentials");
        return ESP_FAIL;
    }

    Log::info(kTag, "credentials saved for '%s', rebooting", ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, kPortalDoneHtml);

    (void)Kernel::instance().scheduler().once(2000, &restartLater, nullptr,
                                              Priority::Normal);
    return ESP_OK;
}

void WifiService::dnsTaskEntry(void* arg) {
    static_cast<WifiService*>(arg)->dnsLoop();
}

/// Minimal DNS responder: answers every query with an A record for
/// the AP address. Runs only while provisioning.
void WifiService::dnsLoop() {
    dns_sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_sock_ < 0) {
        Log::error(kTag, "dns socket failed");
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(53);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(dns_sock_, reinterpret_cast<sockaddr*>(&bind_addr),
             sizeof(bind_addr)) < 0) {
        Log::error(kTag, "dns bind failed");
        close(dns_sock_);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t buf[512];
    for (;;) {
        sockaddr_in client = {};
        socklen_t client_len = sizeof(client);
        const int len =
            recvfrom(dns_sock_, buf, sizeof(buf), 0,
                     reinterpret_cast<sockaddr*>(&client), &client_len);
        if (len < 12) {
            continue;
        }

        // Walk the first question to find where to append the answer.
        int pos = 12;
        while (pos < len && buf[pos] != 0) {
            pos += buf[pos] + 1;
        }
        pos += 5;  // terminator + QTYPE + QCLASS
        if (pos > len) {
            continue;
        }

        buf[2] = 0x81;  // response, recursion available
        buf[3] = 0x80;
        buf[6] = 0;  // one answer
        buf[7] = 1;
        buf[8] = buf[9] = buf[10] = buf[11] = 0;  // no NS/AR records

        const uint8_t answer[] = {
            0xC0, 0x0C,              // pointer to question name
            0x00, 0x01,              // type A
            0x00, 0x01,              // class IN
            0x00, 0x00, 0x00, 0x3C,  // TTL 60s
            0x00, 0x04,              // 4 address bytes
            kApAddr[0], kApAddr[1], kApAddr[2], kApAddr[3],
        };
        if (pos + sizeof(answer) > sizeof(buf)) {
            continue;
        }
        memcpy(buf + pos, answer, sizeof(answer));
        (void)sendto(dns_sock_, buf, pos + sizeof(answer), 0,
                     reinterpret_cast<sockaddr*>(&client), client_len);
    }
}

}  // namespace gallus::services
