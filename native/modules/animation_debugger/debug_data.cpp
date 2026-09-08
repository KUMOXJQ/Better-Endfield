#include "debug_data.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace BetterEndfield::AnimationDebugger {
namespace {
std::ostringstream Stream() {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(12);
    return out;
}
std::string Num(Number value, const char* missing = "null") {
    if (!value || !std::isfinite(*value)) return missing;
    auto out = Stream(); out << *value; return out.str();
}
std::string Bool(std::optional<bool> value, const char* missing = "null") {
    return value ? (*value ? "true" : "false") : missing;
}
size_t Size(const Sample& s) {
    size_t bytes = sizeof(s) + s.target_id.size() + s.character.size() + s.animator.size()
        + s.mode.size() + s.status.size() + s.graph_id.size() + s.graph_status.size();
    for (const auto& c : s.clips) bytes += sizeof(c) + c.id.size() + c.name.size() + c.path.size() + c.source.size() + c.activity.size();
    for (const auto& n : s.nodes) bytes += sizeof(n) + n.id.size() + n.path.size() + n.type.size();
    for (const auto& l : s.layers) bytes += sizeof(l) + l.name.size();
    for (const auto& i : s.issues) bytes += sizeof(i) + i.size();
    // Reserve for vector capacity, allocator overhead and associated events.
    return bytes * 3 + 4096;
}
std::set<std::string> ClipKeys(const Sample& s) {
    std::set<std::string> keys;
    for (const auto& c : s.clips) keys.insert(c.source + ":" + c.path + ":" + c.id);
    return keys;
}
std::string GraphKey(const Sample& s) {
    std::string key = s.graph_id + ":" + s.graph_status;
    for (const auto& n : s.nodes) key += "|" + n.path + ":" + n.id + ":" + n.type;
    return key;
}
void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot create export file");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) throw std::runtime_error("Export write failed (disk full or access denied)");
    out.close();
    if (!out) throw std::runtime_error("Export close failed");
}
}
void Session::Start(std::string session_id, std::string utc, int hz, double seconds, size_t bytes) {
    *this = Session{};
    id = std::move(session_id); started_utc = std::move(utc);
    sample_hz = std::clamp(hz, 1, 120); max_seconds = std::clamp(seconds, 1.0, 1800.0);
    max_bytes = std::clamp<size_t>(bytes, 1024, 256 * 1024 * 1024);
    recording = true;
    events.push_back({0, "session_started", "periodic observation; changes occur between samples"});
}
void Session::Stop(const std::string& reason, double time, const std::string& utc) {
    if (!recording) return;
    recording = false; stop_reason = reason; stopped_utc = utc;
    events.push_back({time, "session_stopped", reason});
}
bool Session::Append(Sample s, const std::string& utc) {
    if (!recording) return false;
    if (!std::isfinite(s.time) || s.time < 0 || (!samples.empty() && s.time <= samples.back().time))
        return false;
    if (s.time >= max_seconds) { Stop("duration_limit", s.time, utc); return false; }
    const size_t bytes = Size(s);
    if (bytes > max_bytes || estimated_bytes > max_bytes - bytes) {
        Stop("capacity_limit", s.time, utc); return false;
    }
    const size_t old_event_count = events.size();
    s.sequence = samples.size();
    if (!samples.empty()) {
        const auto& prev = samples.back();
        if (s.time - prev.time > 2.5 / sample_hz)
            events.push_back({s.time, "sampling_gap", Num(s.time - prev.time) + " seconds since previous sample"});
        if (s.target_id != prev.target_id || s.mode != prev.mode)
            events.push_back({s.time, "target_changed", prev.target_id + " -> " + s.target_id + " (" + s.mode + ")"});
        if (s.status != prev.status) events.push_back({s.time, "quality_changed", s.status});
        if (GraphKey(s) != GraphKey(prev)) events.push_back({s.time, "graph_changed", s.graph_id});
        if (s.target_id == prev.target_id && s.status == "valid" && prev.status == "valid") {
            auto before = ClipKeys(prev), after = ClipKeys(s);
            for (const auto& key : after) if (!before.contains(key)) events.push_back({s.time, "clip_enter_observed", key});
            for (const auto& key : before) if (!after.contains(key)) events.push_back({s.time, "clip_exit_observed", key});
            for (const auto& layer : s.layers) {
                auto it = std::find_if(prev.layers.begin(), prev.layers.end(), [&](const Layer& l) { return l.index == layer.index; });
                if (it == prev.layers.end() || it->current_hash != layer.current_hash || it->next_hash != layer.next_hash || it->transition != layer.transition)
                    events.push_back({s.time, "state_change_observed", "layer " + std::to_string(layer.index) + " hash " + Num(layer.current_hash)});
            }
        }
    }
    if (samples.empty() || s.issues != samples.back().issues) {
        for (const auto& issue : s.issues) events.push_back({s.time, "data_issue", issue});
    }
    size_t event_bytes = 0;
    for (size_t i = old_event_count; i < events.size(); ++i)
        event_bytes += 3 * (sizeof(Event) + events[i].kind.size() + events[i].detail.size());
    if (event_bytes > max_bytes - estimated_bytes - bytes) {
        events.resize(old_event_count);
        Stop("capacity_limit", s.time, utc);
        return false;
    }
    estimated_bytes += bytes + event_bytes;
    samples.push_back(std::move(s));
    return true;
}
std::string JsonString(const std::string& text) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch < 0x20) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += static_cast<char>(ch);
    }
    return out + '"';
}
std::string CsvString(const std::string& text) {
    std::string out = "\"";
    for (char ch : text) { if (ch == '"') out += '"'; out += ch; }
    return out + '"';
}
std::string ToJson(const Session& session) {
    auto out = Stream();
    out << "{\"schema_version\":1,\"tool_version\":\"0.1.0\",\"game_version\":null,\"session_id\":" << JsonString(session.id)
        << ",\"started_utc\":" << JsonString(session.started_utc) << ",\"stopped_utc\":" << JsonString(session.stopped_utc)
        << ",\"recording\":" << (session.recording ? "true" : "false") << ",\"stop_reason\":" << JsonString(session.stop_reason)
        << ",\"sample_hz\":" << session.sample_hz << ",\"max_seconds\":" << session.max_seconds
        << ",\"max_estimated_bytes\":" << session.max_bytes << ",\"time_unit\":\"seconds\",\"samples\":[";
    bool first = true;
    for (const auto& s : session.samples) {
        if (!first) out << ','; first = false;
        out << "{\"sequence\":" << s.sequence << ",\"time\":" << Num(s.time)
            << ",\"target_id\":" << JsonString(s.target_id) << ",\"character\":" << JsonString(s.character)
            << ",\"animator\":" << JsonString(s.animator) << ",\"mode\":" << JsonString(s.mode)
            << ",\"status\":" << JsonString(s.status) << ",\"animator_speed\":" << Num(s.animator_speed)
            << ",\"graph_id\":" << JsonString(s.graph_id) << ",\"graph_status\":" << JsonString(s.graph_status) << ",\"layers\":[";
        bool f = true;
        for (const auto& l : s.layers) {
            if (!f) out << ','; f = false;
            out << "{\"index\":" << l.index << ",\"name\":" << JsonString(l.name) << ",\"weight\":" << Num(l.weight)
                << ",\"current_hash\":" << Num(l.current_hash) << ",\"next_hash\":" << Num(l.next_hash)
                << ",\"normalized_time\":" << Num(l.normalized_time) << ",\"length\":" << Num(l.length)
                << ",\"transition\":" << Bool(l.transition) << '}';
        }
        out << "],\"clips\":["; f = true;
        for (const auto& c : s.clips) {
            if (!f) out << ','; f = false;
            out << "{\"id\":" << JsonString(c.id) << ",\"name\":" << JsonString(c.name) << ",\"path\":" << JsonString(c.path)
                << ",\"source\":" << JsonString(c.source) << ",\"time\":" << Num(c.time) << ",\"length\":" << Num(c.length)
                << ",\"speed\":" << Num(c.speed) << ",\"weight\":" << Num(c.weight) << ",\"loop\":" << Bool(c.loop)
                << ",\"activity\":" << JsonString(c.activity) << '}';
        }
        out << "],\"nodes\":["; f = true;
        for (const auto& n : s.nodes) {
            if (!f) out << ','; f = false;
            out << "{\"id\":" << JsonString(n.id) << ",\"path\":" << JsonString(n.path) << ",\"type\":" << JsonString(n.type)
                << ",\"time\":" << Num(n.time) << ",\"speed\":" << Num(n.speed) << ",\"weight\":" << Num(n.weight)
                << ",\"play_state\":" << Num(n.play_state) << '}';
        }
        out << "],\"issues\":["; f = true;
        for (const auto& issue : s.issues) { if (!f) out << ','; f = false; out << JsonString(issue); }
        out << "]}";
    }
    out << "],\"events\":["; first = true;
    for (const auto& e : session.events) {
        if (!first) out << ','; first = false;
        out << "{\"time\":" << Num(e.time) << ",\"kind\":" << JsonString(e.kind) << ",\"detail\":" << JsonString(e.detail) << '}';
    }
    out << "]}\n"; return out.str();
}
std::string SamplesCsv(const Session& session) {
    auto out = Stream();
    out << "session_id,sequence,time,target_id,character,animator,mode,status,graph_id,graph_status,clip_id,clip_name,path,source,clip_time,length,speed,weight,loop,activity\r\n";
    for (const auto& s : session.samples) {
        auto row = [&](const Clip& c) {
            out << CsvString(session.id) << ',' << s.sequence << ',' << Num(s.time, "") << ',' << CsvString(s.target_id)
                << ',' << CsvString(s.character) << ',' << CsvString(s.animator) << ',' << CsvString(s.mode) << ',' << CsvString(s.status)
                << ',' << CsvString(s.graph_id) << ',' << CsvString(s.graph_status) << ',' << CsvString(c.id) << ',' << CsvString(c.name)
                << ',' << CsvString(c.path) << ',' << CsvString(c.source) << ',' << Num(c.time, "") << ',' << Num(c.length, "")
                << ',' << Num(c.speed, "") << ',' << Num(c.weight, "") << ',' << Bool(c.loop, "") << ',' << CsvString(c.activity) << "\r\n";
        };
        if (s.clips.empty()) row(Clip{}); else for (const auto& c : s.clips) row(c);
    }
    return out.str();
}
std::string EventsCsv(const Session& session) {
    auto out = Stream(); out << "session_id,time,kind,detail\r\n";
    for (const auto& e : session.events) out << CsvString(session.id) << ',' << Num(e.time) << ',' << CsvString(e.kind) << ',' << CsvString(e.detail) << "\r\n";
    return out.str();
}
std::filesystem::path Export(const Session& session, const std::filesystem::path& root) {
    if (session.recording || session.id.empty()) throw std::runtime_error("Stop a session before exporting");
    std::filesystem::create_directories(root);
    std::filesystem::path destination;
    for (unsigned n = 0; n < 10000; ++n) {
        destination = root / (session.id + "-" + std::to_string(n));
        if (std::filesystem::create_directory(destination)) break;
        if (n == 9999) throw std::runtime_error("No unused export directory");
    }
    Write(destination / "session.json", ToJson(session));
    Write(destination / "samples.csv", SamplesCsv(session));
    Write(destination / "events.csv", EventsCsv(session));
    Write(destination / "COMPLETE.txt", "schema_version=1\nAll files written successfully.\n");
    return destination;
}
std::string Describe(const Sample& s, const std::string& filter, bool graph) {
    auto out = Stream();
    out << "Target: " << s.character << " / " << s.animator << " [" << s.target_id << "]\r\n"
        << "Status: " << s.status << "   Mode: " << s.mode << "   Time: " << s.time << " s\r\n"
        << "Animator speed: " << Num(s.animator_speed, "unknown") << "\r\n";
    for (const auto& l : s.layers) out << "Layer " << l.index << " " << l.name << "  state hash=" << Num(l.current_hash, "unknown")
        << " next=" << Num(l.next_hash, "unknown") << " transition=" << Bool(l.transition, "unknown")
        << " normalized=" << Num(l.normalized_time, "unknown") << " state length=" << Num(l.length, "unknown")
        << " weight=" << Num(l.weight, "unknown") << "\r\n";
    out << "\r\nCLIPS (weights are local; sources are separate observations)\r\n";
    for (const auto& c : s.clips) {
        if (!filter.empty() && c.name.find(filter) == std::string::npos) continue;
        out << c.name << " [" << c.id << "]  " << c.source << "  " << c.path << " (" << c.activity << ")"
            << "\r\n  time=" << Num(c.time, "unknown") << " / " << Num(c.length, "unknown")
            << " s  speed=" << Num(c.speed, "unknown") << " loop=" << Bool(c.loop, "unknown")
            << " weight=" << Num(c.weight, "unknown");
        if (c.time && c.length && *c.length > 0) {
            const double t = c.loop.value_or(false) ? (*c.time / *c.length - std::floor(*c.time / *c.length)) : std::clamp(*c.time / *c.length, 0.0, 1.0);
            out << " progress=" << Num(t * 100) << '%';
        }
        out << "\r\n";
    }
    out << "\r\nGraph: " << s.graph_status << " [" << s.graph_id << "]\r\n";
    if (graph) for (const auto& n : s.nodes) out << n.path << "  " << n.type << " [" << n.id << "]  input weight="
        << Num(n.weight, "unknown") << " time=" << Num(n.time, "unknown") << " speed=" << Num(n.speed, "unknown") << "\r\n";
    for (const auto& issue : s.issues) out << "! " << issue << "\r\n";
    return out.str();
}
}
