/// @file main.cpp
/// @brief Host simulation entry: boots the GallusOS kernel on Linux.

#include <chrono>
#include <cstdio>
#include <thread>

#include "gallus/event.hpp"
#include "gallus/kernel.hpp"

namespace {

constexpr const char* kTag = "HostSim";

volatile bool g_ready = false;
volatile bool g_scheduled = false;

void onSystemReady(const gallus::Event& event, void* /*ctx*/) {
    if (event.id != gallus::EventId::SystemReady) {
        return;
    }
    g_ready = true;
    gallus::Log::info(kTag, "caught SystemReady on host");
}

void scheduledPing(void* /*ctx*/) {
    g_scheduled = true;
    gallus::Log::info(kTag, "scheduler job ran on host");
}

}  // namespace

int main() {
    gallus::Kernel& kernel = gallus::Kernel::instance();

    if (!kernel.init().ok()) {
        std::fprintf(stderr, "host sim: kernel init failed\n");
        return 1;
    }

    const auto sub =
        kernel.events().subscribe(gallus::EventId::SystemReady, &onSystemReady);
    if (!sub.ok()) {
        std::fprintf(stderr, "host sim: subscribe failed\n");
        return 1;
    }

    auto job = kernel.scheduler().once(50, &scheduledPing, nullptr,
                                       gallus::Priority::Normal);
    if (!job.ok()) {
        std::fprintf(stderr, "host sim: schedule failed\n");
        return 1;
    }

    if (!kernel.start().ok()) {
        std::fprintf(stderr, "host sim: kernel start failed\n");
        return 1;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (g_ready && g_scheduled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const bool ok = g_ready && g_scheduled;
    std::printf("host sim: ready=%d scheduled=%d delivered=%u dropped=%u jobs=%zu\n",
                g_ready ? 1 : 0, g_scheduled ? 1 : 0,
                kernel.events().deliveredCount(), kernel.events().droppedCount(),
                kernel.scheduler().activeJobs());

  return ok ? 0 : 1;
}
