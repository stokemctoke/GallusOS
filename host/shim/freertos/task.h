#pragma once

#include "freertos/FreeRTOS.h"

using TaskHandle_t = void*;

using TaskFunction_t = void (*)(void*);

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name,
                       uint32_t stack_depth, void* param,
                       UBaseType_t priority, TaskHandle_t* out_handle);

/// Handle of the calling task (nullptr on non-task host threads).
TaskHandle_t xTaskGetCurrentTaskHandle();

/// Sleep for @p ticks (host shim: 1 tick = 1 ms).
void vTaskDelay(TickType_t ticks);
