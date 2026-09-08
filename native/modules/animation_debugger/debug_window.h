#pragma once
#include "runtime_reader.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace BetterEndfield::AnimationDebugger {
struct DebugChannel {
    std::atomic_bool enabled{false}, stopping{false}, recording{false};
    std::atomic_bool window_ready{false};
    std::atomic_int target_id{0}, sample_hz{30}, refresh_hz{10};
    std::atomic<uint64_t> dropped{0};
    std::mutex mutex;
    std::deque<Sample> pending;
    std::vector<Target> targets;
};
double ClockSeconds();
std::string UtcNow();
// The window thread only consumes native snapshots. It never invokes Unity.
bool RunDebugWindow(DebugChannel& channel, const std::filesystem::path& output_root, bool visible = true);
}
