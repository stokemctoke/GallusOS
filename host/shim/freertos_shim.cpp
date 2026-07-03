#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct HostQueue {
    size_t item_size = 0;
    size_t capacity = 0;
    std::deque<std::vector<uint8_t>> items;
    std::mutex mutex;
    std::condition_variable cv;
};

struct HostSemaphore {
    std::timed_mutex mutex;
};

struct HostTask {
    TaskFunction_t fn = nullptr;
    void* param = nullptr;
    std::thread worker;
};

HostQueue* asQueue(QueueHandle_t handle) {
    return static_cast<HostQueue*>(handle);
}

HostSemaphore* asSem(SemaphoreHandle_t handle) {
    return static_cast<HostSemaphore*>(handle);
}

bool waitForItem(std::unique_lock<std::mutex>& lock, HostQueue* queue,
                 TickType_t wait_ticks) {
    if (wait_ticks == 0) {
        return !queue->items.empty();
    }
    if (wait_ticks == portMAX_DELAY) {
        queue->cv.wait(lock, [&]() { return !queue->items.empty(); });
        return true;
    }
    return queue->cv.wait_for(
        lock, std::chrono::milliseconds(wait_ticks),
        [&]() { return !queue->items.empty(); });
}

}  // namespace

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    if (length == 0 || item_size == 0) {
        return nullptr;
    }
    auto* queue = new HostQueue();
    queue->item_size = item_size;
    queue->capacity = length;
    return queue;
}

void vQueueDelete(QueueHandle_t queue) {
    delete asQueue(queue);
}

BaseType_t xQueueSend(QueueHandle_t queue_handle, const void* item,
                      TickType_t wait_ticks) {
    auto* queue = asQueue(queue_handle);
    if (queue == nullptr || item == nullptr) {
        return pdFALSE;
    }

    std::unique_lock<std::mutex> lock(queue->mutex);
    if (queue->items.size() >= queue->capacity) {
        if (wait_ticks == 0) {
            return pdFALSE;
        }
        if (wait_ticks == portMAX_DELAY) {
            queue->cv.wait(lock, [&]() {
                return queue->items.size() < queue->capacity;
            });
        } else if (!queue->cv.wait_for(
                       lock, std::chrono::milliseconds(wait_ticks),
                       [&]() {
                           return queue->items.size() < queue->capacity;
                       })) {
            return pdFALSE;
        }
    }

    std::vector<uint8_t> bytes(queue->item_size);
    std::memcpy(bytes.data(), item, queue->item_size);
    queue->items.push_back(std::move(bytes));
    queue->cv.notify_one();
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue_handle, void* item,
                         TickType_t wait_ticks) {
    auto* queue = asQueue(queue_handle);
    if (queue == nullptr || item == nullptr) {
        return pdFALSE;
    }

    std::unique_lock<std::mutex> lock(queue->mutex);
    if (!waitForItem(lock, queue, wait_ticks)) {
        return pdFALSE;
    }

    std::vector<uint8_t> bytes = std::move(queue->items.front());
    queue->items.pop_front();
    std::memcpy(item, bytes.data(), queue->item_size);
    queue->cv.notify_one();
    return pdTRUE;
}

BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void* item,
                             BaseType_t* higher_prio_task_woken) {
    if (higher_prio_task_woken != nullptr) {
        *higher_prio_task_woken = pdFALSE;
    }
    return xQueueSend(queue, item, 0);
}

SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new HostSemaphore();
}

void vSemaphoreDelete(SemaphoreHandle_t sem) {
    delete asSem(sem);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem_handle, TickType_t wait_ticks) {
    auto* sem = asSem(sem_handle);
    if (sem == nullptr) {
        return pdFALSE;
    }

    if (wait_ticks == portMAX_DELAY) {
        sem->mutex.lock();
        return pdTRUE;
    }
    if (wait_ticks == 0) {
        return sem->mutex.try_lock() ? pdTRUE : pdFALSE;
    }
    return sem->mutex.try_lock_for(std::chrono::milliseconds(wait_ticks))
               ? pdTRUE
               : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem_handle) {
    auto* sem = asSem(sem_handle);
    if (sem == nullptr) {
        return pdFALSE;
    }
    sem->mutex.unlock();
    return pdTRUE;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char* /*name*/,
                       uint32_t /*stack_depth*/, void* param,
                       UBaseType_t /*priority*/, TaskHandle_t* out_handle) {
    if (fn == nullptr) {
        return pdFAIL;
    }

    auto* task = new HostTask();
    task->fn = fn;
    task->param = param;
    task->worker = std::thread([task]() { task->fn(task->param); });
    task->worker.detach();

    if (out_handle != nullptr) {
        *out_handle = task;
    }
    return pdPASS;
}
