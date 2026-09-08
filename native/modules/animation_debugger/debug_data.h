#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace BetterEndfield::AnimationDebugger {
using Number = std::optional<double>;
struct Clip {
    std::string id, name, path, source;
    Number time, length, speed, weight;
    std::optional<bool> loop;
    std::string activity = "unknown";
};
struct Layer {
    int index = 0;
    std::string name;
    Number weight, current_hash, next_hash, normalized_time, length;
    std::optional<bool> transition;
};
struct Node {
    std::string id, path, type;
    Number time, speed, weight, play_state;
};
struct Sample {
    uint64_t sequence = 0;
    double time = 0;
    std::string target_id, character, animator, mode = "follow", status = "no_target";
    std::string graph_id, graph_status = "unavailable";
    Number animator_speed;
    std::vector<Layer> layers;
    std::vector<Clip> clips;
    std::vector<Node> nodes;
    std::vector<std::string> issues;
};
struct Event {
    double time = 0;
    std::string kind, detail;
};
struct Session {
    std::string id, started_utc, stopped_utc, stop_reason;
    int sample_hz = 30;
    double max_seconds = 1800;
    size_t max_bytes = 64 * 1024 * 1024;
    size_t estimated_bytes = 0;
    bool recording = false;
    std::vector<Sample> samples;
    std::vector<Event> events;

    void Start(std::string session_id, std::string utc, int hz,
        double seconds, size_t bytes);
    void Stop(const std::string& reason, double time, const std::string& utc);
    bool Append(Sample sample, const std::string& utc);
};
std::string JsonString(const std::string& text);
std::string CsvString(const std::string& text);
std::string ToJson(const Session& session);
std::string SamplesCsv(const Session& session);
std::string EventsCsv(const Session& session);
// Creates a new directory atomically. On error the session remains in memory
// and the partial export is retained for diagnosis; retry uses a new directory.
std::filesystem::path Export(const Session& session, const std::filesystem::path& root);
std::string Describe(const Sample& sample, const std::string& filter, bool graph);
}
