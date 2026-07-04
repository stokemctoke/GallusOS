#include "gallus/scheduler.hpp"

#include "gallus/log.hpp"

namespace gallus {

namespace {

constexpr const char* kTag = "Scheduler";
constexpr uint32_t kWorkerStackBytes = 4096;
constexpr size_t kWorkerQueueDepth = 16;

/// FreeRTOS priority per tier. Fast sits above the event bus
/// dispatcher (8); Background barely above idle.
constexpr UBaseType_t kTierTaskPriority[Scheduler::kTierCount] = {10, 6, 4, 2};

constexpr const char* kTierTaskName[Scheduler::kTierCount] = {
    "gallus_fast",
    "gallus_normal",
    "gallus_slow",
    "gallus_bg",
};

/// Pack a job identity into the esp_timer callback arg. The slot and
/// generation are baked in at creation, so a callback that was already
/// in flight when its job was cancelled (and the slot recycled) can be
/// detected — the mutable Job struct alone cannot tell those apart.
void* packTimerArg(uint8_t slot, uint16_t generation) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(slot) |
                                   (static_cast<uintptr_t>(generation) << 8));
}

}  // namespace

Scheduler* Scheduler::s_instance_ = nullptr;

Status Scheduler::init() {
    if (initialized_) {
        return Error::InvalidState;
    }
    // One scheduler per system (owned by the Kernel); the timer
    // callback needs a static way back to it because the packed arg
    // has no room for a pointer.
    s_instance_ = this;

    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        return Error::NoMemory;
    }

    for (size_t tier = 0; tier < kTierCount; ++tier) {
        tier_queues_[tier] = xQueueCreate(kWorkerQueueDepth, sizeof(WorkItem));
        if (tier_queues_[tier] == nullptr) {
            return Error::NoMemory;
        }

        worker_ctx_[tier].scheduler = this;
        worker_ctx_[tier].tier = static_cast<uint8_t>(tier);

        const BaseType_t created = xTaskCreate(
            &Scheduler::workerTaskEntry, kTierTaskName[tier],
            kWorkerStackBytes, &worker_ctx_[tier], kTierTaskPriority[tier],
            &tier_tasks_[tier]);
        if (created != pdPASS) {
            return Error::NoMemory;
        }
    }

    initialized_ = true;
    return Status::success();
}

Result<JobHandle> Scheduler::every(uint32_t period_ms, JobFn fn, void* ctx,
                                   Priority priority) {
    return schedule(period_ms, fn, ctx, priority, /*periodic=*/true);
}

Result<JobHandle> Scheduler::once(uint32_t delay_ms, JobFn fn, void* ctx,
                                  Priority priority) {
    return schedule(delay_ms, fn, ctx, priority, /*periodic=*/false);
}

Result<JobHandle> Scheduler::schedule(uint32_t interval_ms, JobFn fn,
                                      void* ctx, Priority priority,
                                      bool periodic) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (fn == nullptr || interval_ms == 0) {
        return Error::InvalidArg;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);

    Job* job = nullptr;
    for (size_t i = 0; i < kMaxJobs; ++i) {
        if (!jobs_[i].active) {
            job = &jobs_[i];
            job->slot = static_cast<uint8_t>(i);
            break;
        }
    }
    if (job == nullptr) {
        xSemaphoreGive(mutex_);
        Log::error(kTag, "job table full (%u slots)",
                   static_cast<unsigned>(kMaxJobs));
        return Error::NoMemory;
    }

    job->fn = fn;
    job->ctx = ctx;
    job->priority = priority;
    job->periodic = periodic;
    job->generation++;

    const esp_timer_create_args_t timer_args = {
        .callback = &Scheduler::timerCallback,
        .arg = packTimerArg(job->slot, job->generation),
        .dispatch_method = ESP_TIMER_TASK,
        .name = "gallus_job",
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&timer_args, &job->timer);
    if (err != ESP_OK) {
        job->timer = nullptr;
        xSemaphoreGive(mutex_);
        return fromEspErr(err);
    }

    const uint64_t interval_us = static_cast<uint64_t>(interval_ms) * 1000;
    err = periodic ? esp_timer_start_periodic(job->timer, interval_us)
                   : esp_timer_start_once(job->timer, interval_us);
    if (err != ESP_OK) {
        esp_timer_delete(job->timer);
        job->timer = nullptr;
        xSemaphoreGive(mutex_);
        return fromEspErr(err);
    }

    job->active = true;

    JobHandle handle;
    handle.slot = job->slot;
    handle.generation = job->generation;
    xSemaphoreGive(mutex_);
    return handle;
}

Status Scheduler::cancel(JobHandle handle) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (!handle.valid() || handle.slot >= kMaxJobs) {
        return Error::InvalidArg;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    Job& job = jobs_[handle.slot];
    if (!job.active || job.generation != handle.generation) {
        xSemaphoreGive(mutex_);
        return Error::NotFound;
    }
    const uint8_t tier = static_cast<uint8_t>(job.priority);
    const esp_timer_handle_t timer = detachTimerLocked(job);
    xSemaphoreGive(mutex_);
    stopTimer(timer);

    // Fence: wait for an in-flight execution of this job to return so
    // the caller may free the job's ctx immediately after cancel().
    // (A queued-but-not-started item is skipped by the worker's
    // generation check.) Skip when called from the job's own tier
    // worker — waiting on ourselves would deadlock.
    if (xTaskGetCurrentTaskHandle() != tier_tasks_[tier]) {
        for (;;) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool running =
                running_slot_[tier] == handle.slot &&
                running_generation_[tier] == handle.generation;
            xSemaphoreGive(mutex_);
            if (!running) {
                break;
            }
            vTaskDelay(1);
        }
    }
    return Status::success();
}

size_t Scheduler::activeJobs() const {
    size_t count = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < kMaxJobs; ++i) {
        if (jobs_[i].active) {
            ++count;
        }
    }
    xSemaphoreGive(mutex_);
    return count;
}

/// Stops and deletes the job's timer and frees the slot. Caller holds
/// mutex_. Returns the timer handle so the caller can stop it after
/// releasing the lock (avoids deadlocks with the host esp_timer shim).
esp_timer_handle_t Scheduler::detachTimerLocked(Job& job) {
    esp_timer_handle_t timer = job.timer;
    job.timer = nullptr;
    job.active = false;
    job.fn = nullptr;
    job.ctx = nullptr;
    return timer;
}

void Scheduler::stopTimer(esp_timer_handle_t timer) {
    if (timer == nullptr) {
        return;
    }
    (void)esp_timer_stop(timer);
    (void)esp_timer_delete(timer);
}

/// Runs on the esp_timer task: validate the job and hand it to the
/// tier worker. Never executes user code here.
void Scheduler::timerCallback(void* arg) {
    Scheduler* self = s_instance_;
    if (self == nullptr) {
        return;
    }
    const uintptr_t packed = reinterpret_cast<uintptr_t>(arg);
    const uint8_t slot = static_cast<uint8_t>(packed & 0xFF);
    const uint16_t generation =
        static_cast<uint16_t>((packed >> 8) & 0xFFFF);

    WorkItem item;
    xSemaphoreTake(self->mutex_, portMAX_DELAY);
    Job* job = &self->jobs_[slot];
    if (!job->active || job->generation != generation) {
        xSemaphoreGive(self->mutex_);
        return;  // cancelled (or slot recycled) between fire and now
    }
    item.fn = job->fn;
    item.ctx = job->ctx;
    item.slot = job->slot;
    item.generation = job->generation;
    item.release_after_run = !job->periodic;
    const uint8_t tier = static_cast<uint8_t>(job->priority);
    xSemaphoreGive(self->mutex_);

    if (xQueueSend(self->tier_queues_[tier], &item, 0) != pdTRUE) {
        Log::warn(kTag, "tier %u queue full, execution skipped",
                  static_cast<unsigned>(tier));
        if (item.release_after_run) {
            // One-shot: the dropped WorkItem was the only thing that
            // would have released the slot and deleted the timer, so
            // clean up here. esp_timer allows stop/delete from the
            // timer's own callback.
            esp_timer_handle_t timer = nullptr;
            xSemaphoreTake(self->mutex_, portMAX_DELAY);
            Job& dropped = self->jobs_[item.slot];
            if (dropped.active && dropped.generation == item.generation) {
                timer = self->detachTimerLocked(dropped);
            }
            xSemaphoreGive(self->mutex_);
            stopTimer(timer);
        }
    }
}

void Scheduler::workerTaskEntry(void* arg) {
    auto* ctx = static_cast<WorkerContext*>(arg);
    Scheduler* self = ctx->scheduler;
    QueueHandle_t queue = self->tier_queues_[ctx->tier];

    WorkItem item;
    for (;;) {
        if (xQueueReceive(queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Revalidate: the job may have been cancelled after this item
        // was queued. Mark it running so cancel() can fence on us.
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        Job& job = self->jobs_[item.slot];
        const bool stale =
            !job.active || job.generation != item.generation;
        if (!stale) {
            self->running_slot_[ctx->tier] = item.slot;
            self->running_generation_[ctx->tier] = item.generation;
        }
        xSemaphoreGive(self->mutex_);
        if (stale) {
            continue;
        }

        item.fn(item.ctx);

        esp_timer_handle_t timer = nullptr;
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        self->running_slot_[ctx->tier] = kNoSlot;
        if (item.release_after_run) {
            Job& done = self->jobs_[item.slot];
            if (done.active && done.generation == item.generation) {
                timer = self->detachTimerLocked(done);
            }
        }
        xSemaphoreGive(self->mutex_);
        stopTimer(timer);
    }
}

}  // namespace gallus
