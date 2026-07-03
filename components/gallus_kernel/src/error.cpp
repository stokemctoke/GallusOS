#include "gallus/error.hpp"

namespace gallus {

const char* toString(Error err) {
    switch (err) {
        case Error::None:             return "None";
        case Error::Timeout:          return "Timeout";
        case Error::NoMemory:         return "NoMemory";
        case Error::InvalidArg:       return "InvalidArg";
        case Error::InvalidState:     return "InvalidState";
        case Error::NotFound:         return "NotFound";
        case Error::NotSupported:     return "NotSupported";
        case Error::Busy:             return "Busy";
        case Error::QueueFull:        return "QueueFull";
        case Error::PermissionDenied: return "PermissionDenied";
        case Error::Hardware:         return "Hardware";
        case Error::Internal:         return "Internal";
    }
    return "Unknown";
}

Error fromEspErr(esp_err_t err) {
    switch (err) {
        case ESP_OK:                    return Error::None;
        case ESP_ERR_TIMEOUT:           return Error::Timeout;
        case ESP_ERR_NO_MEM:            return Error::NoMemory;
        case ESP_ERR_INVALID_ARG:       return Error::InvalidArg;
        case ESP_ERR_INVALID_STATE:     return Error::InvalidState;
        case ESP_ERR_NOT_FOUND:         return Error::NotFound;
        case ESP_ERR_NOT_SUPPORTED:     return Error::NotSupported;
        case ESP_ERR_INVALID_RESPONSE:  return Error::Hardware;
        case ESP_ERR_INVALID_CRC:       return Error::Hardware;
        case ESP_FAIL:                  return Error::Internal;
        default:                        return Error::Internal;
    }
}

esp_err_t toEspErr(Error err) {
    switch (err) {
        case Error::None:             return ESP_OK;
        case Error::Timeout:          return ESP_ERR_TIMEOUT;
        case Error::NoMemory:         return ESP_ERR_NO_MEM;
        case Error::InvalidArg:       return ESP_ERR_INVALID_ARG;
        case Error::InvalidState:     return ESP_ERR_INVALID_STATE;
        case Error::NotFound:         return ESP_ERR_NOT_FOUND;
        case Error::NotSupported:     return ESP_ERR_NOT_SUPPORTED;
        case Error::Busy:             return ESP_ERR_INVALID_STATE;
        case Error::QueueFull:        return ESP_ERR_NO_MEM;
        case Error::PermissionDenied: return ESP_ERR_INVALID_STATE;
        case Error::Hardware:         return ESP_FAIL;
        case Error::Internal:         return ESP_FAIL;
    }
    return ESP_FAIL;
}

}  // namespace gallus
