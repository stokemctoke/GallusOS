#include "gallus/services/rest_service.hpp"

#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "REST";
}

Status RestService::init() {
    Log::info(kTag, "host stub — no HTTP server");
    return Status::success();
}

Status RestService::registerRoute(httpd_method_t /*method*/, const char* uri,
                                  Handler /*handler*/, void* /*ctx*/) {
    Log::debug(kTag, "route ignored on host: %s", uri != nullptr ? uri : "?");
    return Status::success();
}

Status RestService::registerWebSocket(const char* uri, Handler /*handler*/,
                                      void* /*ctx*/) {
    Log::debug(kTag, "websocket ignored on host: %s",
                uri != nullptr ? uri : "?");
    return Status::success();
}

Status RestService::unregisterRoute(httpd_method_t /*method*/,
                                    const char* /*uri*/) {
    return Status::success();
}

bool RestService::authorize(httpd_req_t* /*req*/) const {
    return true;
}

void RestService::reloadToken() {}

}  // namespace gallus::services
