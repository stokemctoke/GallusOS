#pragma once

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

size_t esp_get_free_heap_size(void);
size_t esp_get_minimum_free_heap_size(void);

#ifdef __cplusplus
}
#endif
