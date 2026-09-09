#pragma once
#include "BetterEndfield/ModuleApi.h"
#include "debug_data.h"
#include <map>
#include <set>

namespace BetterEndfield::AnimationDebugger {
struct Target { int id = 0; std::string label; };
// All functions except construction must run on the Unity main thread, or
// during shutdown after callbacks have drained. No Unity objects reach the UI.
class RuntimeReader {
public:
    explicit RuntimeReader(const BE_HostApiV1* host) : host_(host) {}
    bool Resolve();
    Sample Capture(double time, int fixed_id, bool refresh_targets);
    const std::vector<Target>& Targets() const { return targets_; }
    void Release();
private:
    struct Method { BE_ResolvedMethodV1 value{}; bool faulted = false; };
    const BE_HostApiV1* host_;
    std::map<std::string, Method> methods_;
    std::map<std::string, BE_ResolvedFieldV1> fields_;
    BE_ResolvedClassV1 animator_class_{};
    std::vector<uint32_t> temporary_roots_, candidate_roots_;
    std::map<int, void*> candidates_;
    std::vector<Target> targets_;
    uint32_t fixed_root_ = 0;
    void* fixed_object_ = nullptr;
    int fixed_id_ = 0;
    size_t failures_ = 0;
    bool targets_truncated_ = false;
    std::set<std::string> read_issues_;
    bool MethodAt(const char* key, const char* assembly, const char* ns, const char* klass,
        const char* name, const char* params, const char* result, uint32_t count);
    void* Keep(void* object);
    void* Call(const char* key, void* instance = nullptr, void** args = nullptr);
    void* Raw(void* boxed) const;
    std::string Text(void* object) const;
    std::string Name(void* object);
    template<typename T> std::optional<T> Value(void* boxed) const;
    Number Float(const char* key, void* instance, void** args = nullptr);
    Number Double(const char* key, void* instance);
    Number Int(const char* key, void* instance, void** args = nullptr);
    std::optional<bool> Bool(const char* key, void* instance);
    std::string HandleId(void* boxed, const char* klass);
    int ObjectId(void* object);
    void RefreshTargets();
    void ReadLayers(void* animator, Sample& sample);
    void ReadGraph(void* animator, Sample& sample);
    void Walk(void* handle, const std::string& path, Number weight, int depth, bool inactive,
        std::vector<std::string>& ancestry, Sample& sample);
    Clip ReadClip(void* object, const std::string& path, const std::string& source);
};
}
