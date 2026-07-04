#include "gallus/services/diagnostics_service.hpp"

#include <new>

#include "cJSON.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace gallus::services {

cJSON* DiagnosticsService::snapshotJson() const {
    cJSON* doc = cJSON_CreateObject();
    if (doc == nullptr) {
        return nullptr;
    }

    cJSON* heap = cJSON_AddObjectToObject(doc, "heap");
    cJSON_AddNumberToObject(heap, "free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(heap, "min_free", esp_get_minimum_free_heap_size());

    cJSON* events = cJSON_AddObjectToObject(doc, "events");
    cJSON_AddNumberToObject(events, "delivered",
                            kernel_.events().deliveredCount());
    cJSON_AddNumberToObject(events, "dropped",
                            kernel_.events().droppedCount());

    cJSON* scheduler = cJSON_AddObjectToObject(doc, "scheduler");
    cJSON_AddNumberToObject(scheduler, "active_jobs",
                            kernel_.scheduler().activeJobs());

    if (storage_.mounted()) {
        cJSON* fs = cJSON_AddObjectToObject(doc, "filesystem");
        const auto total = storage_.totalBytes();
        const auto used = storage_.usedBytes();
        if (total.ok()) {
            cJSON_AddNumberToObject(fs, "total_bytes", total.value());
        }
        if (used.ok()) {
            cJSON_AddNumberToObject(fs, "used_bytes", used.value());
        }
    }

    cJSON* tasks = cJSON_AddObjectToObject(doc, "tasks");
    cJSON_AddNumberToObject(tasks, "count", uxTaskGetNumberOfTasks());
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    // Bounded replacement for vTaskList(), which writes into its
    // argument with no size limit and would eventually overflow any
    // fixed buffer as the task count grows.
    const UBaseType_t capacity = uxTaskGetNumberOfTasks() + 4;
    auto* states = new (std::nothrow) TaskStatus_t[capacity];
    if (states != nullptr) {
        const UBaseType_t got =
            uxTaskGetSystemState(states, capacity, nullptr);
        cJSON* list = cJSON_AddArrayToObject(tasks, "list");
        for (UBaseType_t i = 0; i < got; ++i) {
            cJSON* row = cJSON_CreateObject();
            cJSON_AddStringToObject(row, "name", states[i].pcTaskName);
            cJSON_AddNumberToObject(row, "prio",
                                    states[i].uxCurrentPriority);
            cJSON_AddNumberToObject(row, "stack_hwm",
                                    states[i].usStackHighWaterMark);
            cJSON_AddNumberToObject(row, "state",
                                    states[i].eCurrentState);
            cJSON_AddItemToArray(list, row);
        }
        delete[] states;
    }
#endif

    return doc;
}

}  // namespace gallus::services
