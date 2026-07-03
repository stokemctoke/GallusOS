#pragma once

#include "freertos/FreeRTOS.h"

using QueueHandle_t = void*;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item,
                      TickType_t wait_ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t wait_ticks);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void* item,
                             BaseType_t* higher_prio_task_woken);
