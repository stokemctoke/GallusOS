#include "esp_timer.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using clock = std::chrono::steady_clock;
const auto kStart = clock::now();

struct TimerState {
    esp_timer_cb_t callback = nullptr;
    void* arg = nullptr;
    uint64_t period_us = 0;
    bool periodic = false;
    std::atomic<bool> running{false};
    std::thread worker;
};

std::mutex g_mutex;
std::unordered_map<esp_timer_handle_t, std::unique_ptr<TimerState>> g_timers;
// Timers deleted from their own callback: the worker thread is
// detached and the state parked here so it stays valid until exit
// (mirrors ESP-IDF, where delete-from-callback is allowed).
std::vector<std::unique_ptr<TimerState>> g_zombies;
uint64_t next_id = 1;

void runTimer(TimerState* state) {
    if (state->periodic) {
        while (state->running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(state->period_us));
            if (!state->running.load(std::memory_order_relaxed)) {
                break;
            }
            if (state->callback != nullptr) {
                state->callback(state->arg);
            }
        }
        return;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(state->period_us));
    if (!state->running.load(std::memory_order_relaxed)) {
        return;
    }
    // Clear running BEFORE the callback: the callback may delete this
    // timer, and state must not be touched after it returns.
    state->running.store(false, std::memory_order_relaxed);
    if (state->callback != nullptr) {
        state->callback(state->arg);
    }
}

}  // namespace

int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(clock::now() -
                                                                 kStart)
        .count();
}

esp_err_t esp_timer_create(const esp_timer_create_args_t* args,
                           esp_timer_handle_t* out_handle) {
    if (args == nullptr || out_handle == nullptr ||
        args->callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    auto state = std::make_unique<TimerState>();
    state->callback = args->callback;
    state->arg = args->arg;

    std::lock_guard<std::mutex> lock(g_mutex);
    const esp_timer_handle_t handle =
        reinterpret_cast<esp_timer_handle_t>(next_id++);
    g_timers[handle] = std::move(state);
    *out_handle = handle;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us) {
    if (timeout_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_timers.find(handle);
    if (it == g_timers.end()) {
        return ESP_ERR_INVALID_STATE;
    }

    TimerState* state = it->second.get();
    if (state->running.load(std::memory_order_relaxed)) {
        return ESP_ERR_INVALID_STATE;
    }

    state->periodic = false;
    state->period_us = timeout_us;
    state->running.store(true, std::memory_order_relaxed);
    if (state->worker.joinable()) {
        state->worker.join();
    }
    state->worker = std::thread(runTimer, state);
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle,
                                     uint64_t period_us) {
    if (period_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_timers.find(handle);
    if (it == g_timers.end()) {
        return ESP_ERR_INVALID_STATE;
    }

    TimerState* state = it->second.get();
    if (state->running.load(std::memory_order_relaxed)) {
        return ESP_ERR_INVALID_STATE;
    }

    state->periodic = true;
    state->period_us = period_us;
    state->running.store(true, std::memory_order_relaxed);
    if (state->worker.joinable()) {
        state->worker.join();
    }
    state->worker = std::thread(runTimer, state);
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_timers.find(handle);
    if (it == g_timers.end()) {
        return ESP_ERR_INVALID_STATE;
    }

    TimerState* state = it->second.get();
    state->running.store(false, std::memory_order_relaxed);
    if (state->worker.joinable()) {
        if (state->worker.get_id() == std::this_thread::get_id()) {
            state->worker.detach();  // stop from own callback
        } else {
            state->worker.join();
        }
    }
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_timers.find(handle);
    if (it == g_timers.end()) {
        return ESP_ERR_INVALID_STATE;
    }

    TimerState* state = it->second.get();
    state->running.store(false, std::memory_order_relaxed);
    if (state->worker.joinable()) {
        if (state->worker.get_id() == std::this_thread::get_id()) {
            // Delete from the timer's own callback: detach and park
            // the state so the still-running thread stays valid.
            state->worker.detach();
            g_zombies.push_back(std::move(it->second));
            g_timers.erase(it);
            return ESP_OK;
        }
        state->worker.join();
    }
    g_timers.erase(it);
    return ESP_OK;
}
