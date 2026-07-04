#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "gallus/error.hpp"

/// @file scheduler.hpp
/// @brief Kernel scheduler: periodic and one-shot jobs on four
/// priority tiers.
///
/// Timing comes from esp_timer; execution happens on one worker task
/// per tier, so a slow Background job can never delay a Fast job.
/// Job storage is static (kMaxJobs), allocated at init.
///
/// NOTE: the ESP32-C5 is single-core. All tiers ultimately share one
/// core with the WiFi stack — callbacks must not busy-loop.

namespace gallus {

/// Execution tiers, highest to lowest FreeRTOS priority.
enum class Priority : uint8_t {
    Fast = 0,    ///< Low-latency work (input, protocol timing).
    Normal,      ///< Default tier.
    Slow,        ///< Housekeeping (telemetry, display refresh).
    Background,  ///< Best-effort work (stats, cleanup).
};

/// Identifies a scheduled job for cancel(). Obtained from every()/once().
struct JobHandle {
    uint8_t slot = 0xFF;
    uint16_t generation = 0;

    [[nodiscard]] bool valid() const { return slot != 0xFF; }
};

class Scheduler {
public:
    /// Job callback. @p ctx is the pointer given at registration.
    using JobFn = void (*)(void* ctx);

    static constexpr size_t kMaxJobs = 32;
    static constexpr size_t kTierCount = 4;

    Scheduler() = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /// Create the per-tier worker tasks and queues.
    Status init();

    /// Run @p fn every @p period_ms milliseconds on tier @p priority.
    Result<JobHandle> every(uint32_t period_ms, JobFn fn, void* ctx = nullptr,
                            Priority priority = Priority::Normal);

    /// Run @p fn once after @p delay_ms milliseconds on tier @p priority.
    /// The job slot is released automatically after it fires.
    Result<JobHandle> once(uint32_t delay_ms, JobFn fn, void* ctx = nullptr,
                           Priority priority = Priority::Normal);

    /// Cancel a scheduled job. Safe with stale handles (NotFound).
    /// On return the job's callback is not running and will not run
    /// again, so its ctx may be freed — unless cancel() is called from
    /// the job's own tier worker (e.g. a job cancelling itself), where
    /// waiting would deadlock and the caller must not free ctx that an
    /// in-flight execution could still touch.
    Status cancel(JobHandle handle);

    /// Number of active jobs.
    [[nodiscard]] size_t activeJobs() const;

private:
    struct Job {
        esp_timer_handle_t timer = nullptr;
        JobFn fn = nullptr;
        void* ctx = nullptr;
        uint16_t generation = 0;
        uint8_t slot = 0;
        Priority priority = Priority::Normal;
        bool periodic = false;
        bool active = false;
    };

    /// What a timer posts to a worker queue.
    struct WorkItem {
        JobFn fn = nullptr;
        void* ctx = nullptr;
        uint8_t slot = 0;
        uint16_t generation = 0;
        bool release_after_run = false;
    };

    Result<JobHandle> schedule(uint32_t interval_ms, JobFn fn, void* ctx,
                               Priority priority, bool periodic);
    static void timerCallback(void* arg);
    static void workerTaskEntry(void* arg);
    esp_timer_handle_t detachTimerLocked(Job& job);
    static void stopTimer(esp_timer_handle_t timer);

    struct WorkerContext {
        Scheduler* scheduler = nullptr;
        uint8_t tier = 0;
    };

    /// Sentinel for running_slot_: no job executing on that tier.
    static constexpr uint8_t kNoSlot = 0xFF;

    /// The single scheduler instance (owned by the Kernel); lets the
    /// esp_timer callback find its way back from a packed slot/gen arg.
    static Scheduler* s_instance_;

    Job jobs_[kMaxJobs] = {};
    WorkerContext worker_ctx_[kTierCount] = {};
    QueueHandle_t tier_queues_[kTierCount] = {};
    TaskHandle_t tier_tasks_[kTierCount] = {};
    uint8_t running_slot_[kTierCount] = {kNoSlot, kNoSlot, kNoSlot, kNoSlot};
    uint16_t running_generation_[kTierCount] = {};
    SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
};

}  // namespace gallus
