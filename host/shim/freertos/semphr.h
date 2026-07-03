#pragma once

#include "freertos/FreeRTOS.h"

using SemaphoreHandle_t = void*;

SemaphoreHandle_t xSemaphoreCreateMutex();
void vSemaphoreDelete(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait_ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
