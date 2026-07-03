#include "esp_system.h"

size_t esp_get_free_heap_size(void) {
    return 256U * 1024U;
}

size_t esp_get_minimum_free_heap_size(void) {
    return 200U * 1024U;
}
