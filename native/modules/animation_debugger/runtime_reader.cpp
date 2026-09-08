#include "runtime_reader.h"
#include <algorithm>
#include <cstring>

namespace BetterEndfield::AnimationDebugger {
namespace {
constexpr auto Core = "UnityEngine.CoreModule.dll";
constexpr auto Anim = "UnityEngine.AnimationModule.dll";
constexpr auto Play = "UnityEngine.Playables";
}
bool RuntimeReader::MethodAt(const char* key, const char* assembly, const char* ns,
    const char* klass, const char* name, const char* params, const char* result, uint32_t count) {
    BE_MethodDescriptorV1 descriptor{assembly, ns, klass, name, params, result, count};
    const bool ok = host_->resolve_method(host_->context, &descriptor, &methods_[key].value) == BE_Result_Ok;
    const std::string message = std::string(ok ? "available: " : "unavailable: ") + key;
    host_->log(host_->context, "betterendfield.animation_debugger", message.c_str());
    return ok;
}
bool RuntimeReader::Resolve() {
    bool ready = host_->resolve_class(host_->context, Anim, "UnityEngine", "Animator", &animator_class_) == BE_Result_Ok;
    ready &= MethodAt("find", Core, "UnityEngine", "Object", "FindObjectsOfType", "System.Type", "UnityEngine.Object[]", 1);
    ready &= MethodAt("id", Core, "UnityEngine", "Object", "GetInstanceID", nullptr, "System.Int32", 0);
    ready &= MethodAt("alive", Core, "UnityEngine", "Object", "op_Implicit", "UnityEngine.Object", "System.Boolean", 1);
    MethodAt("name", Core, "UnityEngine", "Object", "get_name", nullptr, "System.String", 0);
    ready &= MethodAt("array.length", "mscorlib.dll", "System", "Array", "get_Length", nullptr, "System.Int32", 0);
    ready &= MethodAt("array.value", "mscorlib.dll", "System", "Array", "GetValue", "System.Int32", "System.Object", 1);
    MethodAt("player", "Gameplay.Beyond.dll", "Beyond.Gameplay", "GameUtil", "get_playerTrans", nullptr, "UnityEngine.Transform", 0);
    MethodAt("children", Core, "UnityEngine", "Component", "GetComponentsInChildren", "System.Type|System.Boolean", "UnityEngine.Component[]", 2);
    MethodAt("type.name", "mscorlib.dll", "System", "RuntimeType", "get_FullName", nullptr, "System.String", 0);
    auto animator = [&](const char* key, const char* name, const char* params, const char* result, uint32_t count) {
        return MethodAt(key, Anim, "UnityEngine", "Animator", name, params, result, count);
    };
    ready &= animator("layers", "get_layerCount", nullptr, "System.Int32", 0);
    ready &= animator("current.clips", "GetCurrentAnimatorClipInfo", "System.Int32", "UnityEngine.AnimatorClipInfo[]", 1);
    animator("next.clips", "GetNextAnimatorClipInfo", "System.Int32", "UnityEngine.AnimatorClipInfo[]", 1);
    animator("current.state", "GetCurrentAnimatorStateInfo", "System.Int32", "UnityEngine.AnimatorStateInfo", 1);
    animator("next.state", "GetNextAnimatorStateInfo", "System.Int32", "UnityEngine.AnimatorStateInfo", 1);
    animator("transition", "IsInTransition", "System.Int32", "System.Boolean", 1);
    animator("layer.name", "GetLayerName", "System.Int32", "System.String", 1);
    animator("layer.weight", "GetLayerWeight", "System.Int32", "System.Single", 1);
    animator("speed", "get_speed", nullptr, "System.Single", 0);
    animator("graph", "get_playableGraph", nullptr, "UnityEngine.Playables.PlayableGraph", 0);
    MethodAt("state.hash", Anim, "UnityEngine", "AnimatorStateInfo", "get_fullPathHash", nullptr, "System.Int32", 0);
    MethodAt("state.time", Anim, "UnityEngine", "AnimatorStateInfo", "get_normalizedTime", nullptr, "System.Single", 0);
    MethodAt("state.length", Anim, "UnityEngine", "AnimatorStateInfo", "get_length", nullptr, "System.Single", 0);
    ready &= MethodAt("info.clip", Anim, "UnityEngine", "AnimatorClipInfo", "get_clip", nullptr, "UnityEngine.AnimationClip", 0);
    MethodAt("info.weight", Anim, "UnityEngine", "AnimatorClipInfo", "get_weight", nullptr, "System.Single", 0);
    MethodAt("clip.length", Anim, "UnityEngine", "AnimationClip", "get_length", nullptr, "System.Single", 0);
    MethodAt("clip.loop", Anim, "UnityEngine", "Motion", "get_isLooping", nullptr, "System.Boolean", 0);
    MethodAt("graph.valid", Core, Play, "PlayableGraph", "IsValid", nullptr, "System.Boolean", 0);
    MethodAt("graph.playing", Core, Play, "PlayableGraph", "IsPlaying", nullptr, "System.Boolean", 0);
    MethodAt("graph.outputs", Core, Play, "PlayableGraph", "GetOutputCount", nullptr, "System.Int32", 0);
    MethodAt("graph.output", Core, Play, "PlayableGraph", "GetOutput", "System.Int32", "UnityEngine.Playables.PlayableOutput", 1);
    MethodAt("output.handle", Core, Play, "PlayableOutput", "GetHandle", nullptr, "UnityEngine.Playables.PlayableOutputHandle", 0);
    MethodAt("output.type", Core, Play, "PlayableOutputHandle", "GetPlayableOutputType", nullptr, "System.Type", 0);
    MethodAt("output.source", Core, Play, "PlayableOutputHandle", "GetSourcePlayable", nullptr, "UnityEngine.Playables.PlayableHandle", 0);
    MethodAt("output.target", Anim, "UnityEngine.Animations", "AnimationPlayableOutput", "InternalGetTarget", "UnityEngine.Playables.PlayableOutputHandle&", "UnityEngine.Animator", 1);
    auto handle = [&](const char* key, const char* name, const char* params, const char* result, uint32_t count) {
        MethodAt(key, Core, Play, "PlayableHandle", name, params, result, count);
    };
    handle("node.valid", "IsValid", nullptr, "System.Boolean", 0);
    handle("node.type", "GetPlayableType", nullptr, "System.Type", 0);
    handle("node.time", "GetTime", nullptr, "System.Double", 0);
    handle("node.speed", "GetSpeed", nullptr, "System.Double", 0);
    handle("node.state", "GetPlayState", nullptr, "UnityEngine.Playables.PlayState", 0);
    handle("node.count", "GetInputCount", nullptr, "System.Int32", 0);
    handle("node.input", "GetInputHandle", "System.Int32", "UnityEngine.Playables.PlayableHandle", 1);
    handle("node.weight", "GetInputWeight", "System.Int32", "System.Single", 1);
    MethodAt("node.clip", Anim, "UnityEngine.Animations", "AnimationClipPlayable", "GetAnimationClipInternal", "UnityEngine.Playables.PlayableHandle&", "UnityEngine.AnimationClip", 1);
    for (const char* klass : {"PlayableHandle", "PlayableGraph"}) {
        for (const char* field : {"m_Handle", "m_Version"}) {
            BE_FieldDescriptorV1 d{Core, Play, klass, field, field == std::string("m_Handle") ? "System.IntPtr" : "System.UInt32"};
            host_->resolve_field(host_->context, &d, &fields_[std::string(klass) + "." + field]);
        }
    }
    return ready;
}
void* RuntimeReader::Keep(void* object) {
    if (object) {
        const uint32_t root = host_->gchandle_new(host_->context, object, 1);
        if (!root) { ++failures_; return nullptr; }
        temporary_roots_.push_back(root);
    }
    return object;
}
void* RuntimeReader::Call(const char* key, void* instance, void** args) {
    const auto it = methods_.find(key);
    if (it == methods_.end() || !it->second.value.method_info) { ++failures_; return nullptr; }
    void* exception = nullptr;
    void* result = host_->runtime_invoke(host_->context, it->second.value.method_info, instance, args, &exception);
    if (exception) { ++failures_; return nullptr; }
    return Keep(result);
}
void* RuntimeReader::Raw(void* boxed) const { return boxed ? host_->object_unbox(host_->context, boxed) : nullptr; }
template<typename T> std::optional<T> RuntimeReader::Value(void* boxed) const {
    void* raw = Raw(boxed);
    if (!raw) return {};
    T value{}; std::memcpy(&value, raw, sizeof(value)); return value;
}
std::string RuntimeReader::Text(void* object) const {
    if (!object) return {};
    char buffer[2048]{};
    host_->copy_managed_string(host_->context, object, buffer, sizeof(buffer));
    return buffer;
}
std::string RuntimeReader::Name(void* object) { return object ? Text(Call("name", object)) : std::string{}; }
Number RuntimeReader::Float(const char* key, void* instance, void** args) {
    const auto v = Value<float>(Call(key, instance, args));
    return v && std::isfinite(*v) ? Number(*v) : Number{};
}
Number RuntimeReader::Double(const char* key, void* instance) {
    const auto v = Value<double>(Call(key, instance));
    return v && std::isfinite(*v) ? Number(*v) : Number{};
}
Number RuntimeReader::Int(const char* key, void* instance, void** args) {
    const auto v = Value<int>(Call(key, instance, args)); return v ? Number(*v) : Number{};
}
std::optional<bool> RuntimeReader::Bool(const char* key, void* instance) { return Value<bool>(Call(key, instance)); }
int RuntimeReader::ObjectId(void* object) { return object ? Value<int>(Call("id", object)).value_or(0) : 0; }
std::string RuntimeReader::HandleId(void* boxed, const char* klass) {
    if (!boxed) return {};
    const auto& pointer = fields_[std::string(klass) + ".m_Handle"];
    const auto& version = fields_[std::string(klass) + ".m_Version"];
    if (!pointer.field_info || !version.field_info) return {};
    const auto p = Value<uintptr_t>(Keep(host_->field_get_value_object(host_->context, pointer.field_info, boxed)));
    const auto v = Value<uint32_t>(Keep(host_->field_get_value_object(host_->context, version.field_info, boxed)));
    return p && v ? std::to_string(*p) + ":" + std::to_string(*v) : std::string{};
}
void RuntimeReader::RefreshTargets() {
    for (auto root : candidate_roots_) host_->gchandle_free(host_->context, root);
    candidate_roots_.clear(); candidates_.clear(); targets_.clear();
    void* args[]{animator_class_.type_object};
    void* array = Call("find", nullptr, args);
    if (!array) return;
    const int length = static_cast<int>(Int("array.length", array).value_or(0));
    targets_truncated_ = length > 256;
    for (int i = 0; i < std::min(length, 256); ++i) {
        void* index[]{&i};
        void* object = Call("array.value", array, index);
        const int id = ObjectId(object);
        if (!id) continue;
        const uint32_t root = host_->gchandle_new(host_->context, object, 1);
        if (!root) continue;
        candidate_roots_.push_back(root); candidates_[id] = object;
        targets_.push_back({id, Name(object) + " [" + std::to_string(id) + "]"});
    }
}
Clip RuntimeReader::ReadClip(void* object, const std::string& path, const std::string& source) {
    Clip clip;
    clip.path = path; clip.source = source;
    if (!object) return clip;
    clip.id = std::to_string(ObjectId(object)); clip.name = Name(object);
    clip.length = Float("clip.length", object); clip.loop = Bool("clip.loop", object);
    return clip;
}
void RuntimeReader::ReadLayers(void* animator, Sample& sample) {
    const int count = static_cast<int>(Int("layers", animator).value_or(0));
    if (count > 32) sample.issues.push_back("layer_limit: 32");
    for (int i = 0; i < std::min(count, 32); ++i) {
        void* args[]{&i};
        Layer layer; layer.index = i;
        layer.name = Text(Call("layer.name", animator, args));
        layer.weight = Float("layer.weight", animator, args);
        layer.transition = Value<bool>(Call("transition", animator, args));
        void* current = Call("current.state", animator, args);
        if (current) {
            layer.current_hash = Int("state.hash", Raw(current));
            layer.normalized_time = Float("state.time", Raw(current));
            layer.length = Float("state.length", Raw(current));
        }
        if (layer.transition.value_or(false)) {
            void* next = Call("next.state", animator, args);
            if (next) layer.next_hash = Int("state.hash", Raw(next));
        }
        sample.layers.push_back(layer);
        for (const char* side : {"current", "next"}) {
            if (side == std::string("next") && !layer.transition.value_or(false)) continue;
            void* clips = Call((std::string(side) + ".clips").c_str(), animator, args);
            if (!clips) continue;
            const int length = static_cast<int>(Int("array.length", clips).value_or(0));
            if (length > 128) sample.issues.push_back("clip_limit_per_layer: 128");
            for (int c = 0; c < std::min(length, 128) && sample.clips.size() < 512; ++c) {
                void* index[]{&c}; void* info = Call("array.value", clips, index);
                if (!info) continue;
                const Number weight = Float("info.weight", Raw(info));
                if (weight && *weight <= 0) continue;
                Clip clip = ReadClip(Call("info.clip", Raw(info)), "layer/" + std::to_string(i) + "/" + side + "/" + std::to_string(c), "animator_clip_info");
                clip.weight = weight;
                clip.activity = "reported_by_animator";
                // State normalized time is not ClipPlayable time. Never invent
                // per-clip time/speed from a blended state's duration.
                sample.clips.push_back(std::move(clip));
            }
        }
    }
    if (sample.clips.size() >= 512) sample.issues.push_back("Animator clip total limit reached: 512");
}
void RuntimeReader::Walk(void* handle, const std::string& path, Number weight, int depth, bool inactive,
    std::vector<std::string>& ancestry, Sample& sample) {
    if (!handle || !Bool("node.valid", Raw(handle)).value_or(false)) return;
    if (depth > 24 || sample.nodes.size() >= 256) {
        sample.issues.push_back("graph_truncated: depth 24 / 256 node paths"); return;
    }
    const std::string id = HandleId(handle, "PlayableHandle");
    if (!id.empty() && std::find(ancestry.begin(), ancestry.end(), id) != ancestry.end()) {
        sample.issues.push_back("graph_cycle: " + path); return;
    }
    Node node; node.id = id; node.path = path; node.weight = weight;
    node.type = Text(Call("type.name", Call("node.type", Raw(handle))));
    node.time = Double("node.time", Raw(handle)); node.speed = Double("node.speed", Raw(handle));
    node.play_state = Int("node.state", Raw(handle));
    sample.nodes.push_back(node);
    inactive = inactive || (weight && *weight <= 0);
    if (!inactive && node.type == "UnityEngine.Animations.AnimationClipPlayable") {
        void* args[]{Raw(handle)};
        Clip clip = ReadClip(Call("node.clip", nullptr, args), path, "playable_local_input");
        clip.time = node.time; clip.speed = node.speed; clip.weight = weight;
        clip.activity = sample.graph_status == "stopped" ? "graph_stopped" : "connected_graph_path_contribution_unverified";
        sample.clips.push_back(std::move(clip));
    }
    const int count = static_cast<int>(Int("node.count", Raw(handle)).value_or(0));
    if (count > 128) sample.issues.push_back("graph_input_limit: 128");
    ancestry.push_back(id);
    for (int i = 0; i < std::min(count, 128); ++i) {
        void* args[]{&i};
        Number input_weight = Float("node.weight", Raw(handle), args);
        // Preserve zero-weight graph branches in the topology; their presence
        // does not assert that they contribute to the final animation output.
        Walk(Call("node.input", Raw(handle), args), path + "/" + std::to_string(i), input_weight, depth + 1, inactive, ancestry, sample);
    }
    ancestry.pop_back();
}
void RuntimeReader::ReadGraph(void* animator, Sample& sample) {
    void* graph = Call("graph", animator);
    if (!graph) { sample.issues.push_back("PlayableGraph unavailable"); return; }
    void* raw = Raw(graph);
    const auto valid = Bool("graph.valid", raw);
    if (!valid.value_or(false)) { sample.graph_status = valid ? "invalid" : "unavailable"; return; }
    const auto playing = Bool("graph.playing", raw);
    sample.graph_status = playing ? (*playing ? "playing" : "stopped") : "valid_play_state_unknown";
    sample.graph_id = HandleId(graph, "PlayableGraph");
    if (sample.graph_id.empty()) sample.issues.push_back("Graph identity unavailable");
    const int outputs = static_cast<int>(Int("graph.outputs", raw).value_or(0));
    if (outputs > 32) sample.issues.push_back("graph_output_limit: 32");
    for (int i = 0; i < std::min(outputs, 32); ++i) {
        void* args[]{&i};
        void* output = Call("graph.output", raw, args);
        if (!output) continue;
        void* handle = Call("output.handle", Raw(output));
        if (!handle) continue;
        const std::string type = Text(Call("type.name", Call("output.type", Raw(handle))));
        if (type != "UnityEngine.Animations.AnimationPlayableOutput") continue;
        void* target_args[]{Raw(handle)};
        void* target = Call("output.target", nullptr, target_args);
        if (ObjectId(target) != ObjectId(animator)) continue;
        std::vector<std::string> ancestry;
        Walk(Call("output.source", Raw(handle)), "output/" + std::to_string(i), {}, 0, false, ancestry, sample);
    }
    if (sample.nodes.empty()) sample.issues.push_back("No readable animation output bound to selected Animator");
}
Sample RuntimeReader::Capture(double time, int fixed_id, bool refresh_targets) {
    for (auto root : temporary_roots_) host_->gchandle_free(host_->context, root);
    temporary_roots_.clear(); failures_ = 0;
    Sample sample; sample.time = time; sample.mode = fixed_id ? "fixed" : "follow";
    if (refresh_targets) RefreshTargets();
    if (targets_truncated_) sample.issues.push_back("Animator discovery truncated to first 256 targets");
    if (fixed_id != fixed_id_) {
        if (fixed_root_) host_->gchandle_free(host_->context, fixed_root_);
        fixed_root_ = 0; fixed_object_ = nullptr; fixed_id_ = fixed_id;
        const auto it = candidates_.find(fixed_id);
        if (it != candidates_.end()) {
            fixed_root_ = host_->gchandle_new(host_->context, it->second, 1);
            if (fixed_root_) fixed_object_ = it->second;
        }
    }
    void* animator = fixed_object_;
    if (!fixed_id) {
        animator = nullptr;
        void* player = Call("player");
        if (player) {
            sample.character = Name(player);
            bool inactive = false; void* args[]{animator_class_.type_object, &inactive};
            void* array = Call("children", player, args);
            if (array) {
                const int count = static_cast<int>(Int("array.length", array).value_or(0));
                if (count == 1) { int zero = 0; void* index[]{&zero}; animator = Call("array.value", array, index); }
                else if (count > 1) sample.issues.push_back("Multiple player Animators: select a fixed target");
            }
        } else sample.issues.push_back("Current player unavailable (select a fixed Animator for login/showcase)");
    }
    if (fixed_id) sample.target_id = std::to_string(fixed_id);
    if (animator) {
        void* args[]{animator};
        if (Value<bool>(Call("alive", nullptr, args)).value_or(false)) {
            sample.target_id = std::to_string(ObjectId(animator));
            sample.animator = Name(animator);
            if (sample.character.empty()) sample.character = sample.animator;
            sample.status = "valid";
            sample.animator_speed = Float("speed", animator);
            ReadLayers(animator, sample); ReadGraph(animator, sample);
        } else sample.status = "target_lost";
    } else if (fixed_id) sample.status = "target_lost";
    if (failures_) sample.issues.push_back("Unavailable/failed runtime reads: " + std::to_string(failures_));
    std::sort(sample.issues.begin(), sample.issues.end());
    sample.issues.erase(std::unique(sample.issues.begin(), sample.issues.end()), sample.issues.end());
    for (auto root : temporary_roots_) host_->gchandle_free(host_->context, root);
    temporary_roots_.clear();
    return sample;
}
void RuntimeReader::Release() {
    for (auto root : temporary_roots_) host_->gchandle_free(host_->context, root);
    for (auto root : candidate_roots_) host_->gchandle_free(host_->context, root);
    if (fixed_root_) host_->gchandle_free(host_->context, fixed_root_);
    temporary_roots_.clear(); candidate_roots_.clear(); fixed_root_ = 0;
    fixed_object_ = nullptr; candidates_.clear(); targets_.clear();
}
}
