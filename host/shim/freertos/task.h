#pragma once

#include "freertos/FreeRTOS.h"

using TaskHandle_t = void*;

using TaskFunction_t = void (*)(void*);

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name,
                       uint32_t stack_depth, void* param,
                       UBaseType_t priority, TaskHandle_t* out_handle);
