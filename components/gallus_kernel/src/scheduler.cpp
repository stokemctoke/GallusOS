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

}  // namespace

Status Scheduler::init() {
    if (initialized_) {
        return Error::InvalidState;
    }

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
    job->owner = this;
    job->priority = priority;
    job->periodic = periodic;
    job->generation++;

    const esp_timer_create_args_t timer_args = {
        .callback = &Scheduler::timerCallback,
        .arg = job,
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
    releaseSlotLocked(job);
    xSemaphoreGive(mutex_);
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
/// mutex_.
void Scheduler::releaseSlotLocked(Job& job) {
    if (job.timer != nullptr) {
        (void)esp_timer_stop(job.timer);  // fails harmlessly if not running
        (void)esp_timer_delete(job.timer);
        job.timer = nullptr;
    }
    job.active = false;
    job.fn = nullptr;
    job.ctx = nullptr;
}

/// Runs on the esp_timer task: validate the job and hand it to the
/// tier worker. Never executes user code here.
void Scheduler::timerCallback(void* arg) {
    Job* job = static_cast<Job*>(arg);
    Scheduler* self = job->owner;

    WorkItem item;
    xSemaphoreTake(self->mutex_, portMAX_DELAY);
    if (!job->active) {
        xSemaphoreGive(self->mutex_);
        return;  // cancelled between dispatch and now
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

        item.fn(item.ctx);

        if (item.release_after_run) {
            // One-shot job: free the slot unless cancel() beat us to it.
            xSemaphoreTake(self->mutex_, portMAX_DELAY);
            Job& job = self->jobs_[item.slot];
            if (job.active && job.generation == item.generation) {
                self->releaseSlotLocked(job);
            }
            xSemaphoreGive(self->mutex_);
        }
    }
}

}  // namespace gallus
