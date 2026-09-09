#include "debug_data.h"
#include "sample_scheduler.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace BetterEndfield::AnimationDebugger;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Sample Fixture(double time) {
    Sample s; s.time = time; s.status = "valid"; s.target_id = "2147483647";
    s.character = "佩丽卡,\"测试\"\r\n"; s.animator = "LoginAnimator";
    s.layers.push_back({0, "Base", 1.0, 100.0, {}, 0.5, 2.0, false});
    s.clips.push_back({"32", "run_loop", "output/0/0", "playable_local_input", 3.0, 2.0, 1.0, 0.72, true});
    s.clips.push_back({"32", "run_loop", "output/0/1", "playable_local_input", 0.5, 2.0, 1.0, 0.28, true});
    return s;
}
}
int main(int argc, char** argv) {
    try {
        Check(JsonString("a\"\\\n\x01") == "\"a\\\"\\\\\\u000a\\u0001\"", "JSON escaping");
        SampleScheduler scheduler;
        int captures = 0;
        for (int frame = 0; frame < 600; ++frame) captures += scheduler.Due(frame / 60.0, 30) ? 1 : 0;
        Check(captures == 300, "30 Hz sampling must not drift on 60 Hz frames");
        Check(scheduler.Due(100, 30) && !scheduler.Due(100, 30), "late frame does not trigger catch-up burst");
        Check(scheduler.Due(100.001, 10), "rate change resets deadline");
        Check(CsvString("a,\"b\"\r\n") == "\"a,\"\"b\"\"\r\n\"", "CSV escaping");
        Session s; s.Start("fixture", "2026-09-08T00:00:00Z", 30, 10, 4 * 1024 * 1024);
        Check(s.Append(Fixture(0), "utc"), "first sample");
        auto next = Fixture(0.04); next.layers[0].current_hash = 200; next.clips.erase(next.clips.begin());
        Check(s.Append(next, "utc"), "transition sample");
        Check(!s.Append(next, "utc"), "duplicate timestamp rejected");
        auto lost = Sample{}; lost.time = 0.5; lost.target_id = s.samples.back().target_id; lost.status = "target_lost";
        Check(s.Append(lost, "utc"), "loss sample retained");
        Check(s.samples.back().clips.empty(), "loss does not carry forward clips");
        bool gap = false, state = false, exit = false;
        for (const auto& e : s.events) { gap |= e.kind == "sampling_gap"; state |= e.kind == "state_change_observed"; exit |= e.kind == "clip_exit_observed"; }
        Check(gap && state && exit, "events derived from samples");
        Check(s.samples[0].clips.size() == 2, "same clip distinct paths retained");
        s.Stop("user", 0.6, "2026-09-08T00:00:01Z");
        Check(!s.Append(Fixture(0.7), "utc"), "stopped session cannot append");
        const auto json = ToJson(s), csv = SamplesCsv(s);
        Check(json.find("\"time\":null") == std::string::npos, "finite times");
        Check(csv.find("target_lost") != std::string::npos, "empty clip row exported");
        Session duration; duration.Start("duration", "utc", 30, 1, 100000);
        Check(!duration.Append(Fixture(1), "utc") && duration.stop_reason == "duration_limit", "duration bound");
        Session capacity; capacity.Start("capacity", "utc", 30, 10, 1024);
        Check(!capacity.Append(Fixture(0), "utc") && capacity.stop_reason == "capacity_limit", "capacity bound");
        Session unknown; unknown.Start("unknown", "utc", 30, 10, 100000);
        auto unknown_sample = Fixture(0); unknown_sample.clips[0].time.reset();
        unknown_sample.clips[0].speed = std::numeric_limits<double>::quiet_NaN();
        unknown.Append(unknown_sample, "utc"); unknown.Stop("user", 1, "utc");
        Check(ToJson(unknown).find("\"time\":null") != std::string::npos, "unknown remains null");
        Check(ToJson(unknown).find("\"speed\":null") != std::string::npos, "nonfinite remains null");
        Check(Describe(unknown_sample, "absent", false).find("run_loop") == std::string::npos, "clip filter");
        Session timing; timing.Start("timing", "utc", 30, 1800, 1000000);
        for (int i = 0; i < 31; ++i) {
            auto sample = Fixture(i / 30.0); sample.capture_ms = 0.2; sample.game_version = "test-version";
            Check(timing.Append(sample, "utc"), "timing append");
        }
        Check(std::abs(timing.ActualSampleHz() - 30) < 1e-6 && timing.runtime_sample_count == 31
            && timing.game_version == "test-version", "actual rate and version metadata");
        auto gap_sample = Sample{}; gap_sample.time = 2; gap_sample.status = "no_main_thread_samples";
        timing.Append(gap_sample, "utc");
        Check(timing.runtime_sample_count == 31, "synthetic gaps not counted as runtime captures");
        Check(ToJson(timing, false).find("\"samples\":[]") != std::string::npos
            && ToJson(timing, false).find("\"capture_p95_ms\":0.2") != std::string::npos, "metadata-only export with P95");
        if (argc > 1) {
            const auto first = Export(s, std::filesystem::path(argv[1]));
            const auto second = Export(s, std::filesystem::path(argv[1]));
            Check(first != second, "export never overwrites");
            Check(std::filesystem::exists(first / "COMPLETE.txt"), "export complete marker");
            Check(std::filesystem::exists(first / "metadata.json"), "standalone metadata exported");
            bool failed = false;
            try { Export(s, first / "session.json"); } catch (const std::exception&) { failed = true; }
            Check(failed && s.samples.size() == 3 && !s.recording, "failed export preserves the stopped session");
            std::cout << first.string() << '\n';
        }
        std::cout << "Animation debugger data checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
