#include "runtime_reader.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

using namespace BetterEndfield::AnimationDebugger;
namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Object {
    alignas(16) unsigned char raw[32]{};
    std::string text;
    std::map<std::string, Object*> properties;
    std::vector<Object*> elements;
};
struct Method { std::string klass, name; };
struct Fake {
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<std::unique_ptr<Method>> methods;
    std::vector<std::unique_ptr<std::string>> fields;
    std::map<uint32_t, void*> roots;
    std::set<std::string> unavailable;
    uint32_t next_root = 0;
    Object *all = nullptr, *player = nullptr, *animator = nullptr, *clip = nullptr, *node = nullptr;
    BE_HostApiV1 api{};
    Object* New() { objects.push_back(std::make_unique<Object>()); return objects.back().get(); }
    template<typename T> Object* Box(T value) { auto* o = New(); std::memcpy(o->raw, &value, sizeof(T)); return o; }
    Object* Text(const std::string& s) { auto* o = New(); o->text = s; return o; }
    Object* Array(std::vector<Object*> values) { auto* o = New(); o->elements = std::move(values); return o; }
    Object* Prop(Object* o, const std::string& key) { const auto it = o->properties.find(key); return it == o->properties.end() ? nullptr : it->second; }
    Object* Handle(uintptr_t id, const std::string& type) {
        auto* o = New();
        o->properties = {{"m_Handle", Box(id)}, {"m_Version", Box(uint32_t{3})}, {"IsValid", Box(true)},
            {"GetPlayableType", Text(type)}, {"GetTime", Box(2.5)}, {"GetSpeed", Box(1.0)}, {"GetPlayState", Box(1)}};
        return o;
    }
    Fake() {
        api.abi_version = BETTER_ENDFIELD_MODULE_ABI_V1; api.context = this;
        api.log = [](void*, const char*, const char*) {};
        api.resolve_method = [](void* c, const BE_MethodDescriptorV1* d, BE_ResolvedMethodV1* out) {
            auto& f = *static_cast<Fake*>(c);
            const std::string key = std::string(d->class_name) + "." + d->method_name;
            if (f.unavailable.contains(key)) return BE_Result_NotFound;
            // Loop is inherited from Motion. The resolver does not search bases.
            if (key == "AnimationClip.get_isLooping") return BE_Result_NotFound;
            f.methods.push_back(std::make_unique<Method>(Method{d->class_name, d->method_name}));
            out->method_info = f.methods.back().get(); out->method_pointer = f.methods.back().get(); return BE_Result_Ok;
        };
        api.resolve_class = [](void* c, const char*, const char*, const char*, BE_ResolvedClassV1* out) {
            out->type_object = static_cast<Fake*>(c)->New(); return BE_Result_Ok;
        };
        api.resolve_field = [](void* c, const BE_FieldDescriptorV1* d, BE_ResolvedFieldV1* out) {
            auto& f = *static_cast<Fake*>(c); f.fields.push_back(std::make_unique<std::string>(d->field_name));
            out->field_info = f.fields.back().get(); return BE_Result_Ok;
        };
        api.field_get_value_object = [](void* c, const void* field, void* instance) -> void* {
            return static_cast<Fake*>(c)->Prop(static_cast<Object*>(instance), *static_cast<const std::string*>(field));
        };
        api.object_unbox = [](void*, void* value) -> void* { return static_cast<Object*>(value)->raw; };
        api.gchandle_new = [](void* c, void* object, int) {
            auto& f = *static_cast<Fake*>(c); f.roots[++f.next_root] = object; return f.next_root;
        };
        api.gchandle_free = [](void* c, uint32_t root) { static_cast<Fake*>(c)->roots.erase(root); };
        api.copy_managed_string = [](void*, const void* object, char* dest, size_t size) {
            const auto& text = static_cast<const Object*>(object)->text;
            const size_t n = std::min(size - 1, text.size()); std::memcpy(dest, text.data(), n); dest[n] = 0; return static_cast<int>(n);
        };
        api.runtime_invoke = [](void* c, const void* info, void* instance, void** args, void** exception) -> void* {
            auto& f = *static_cast<Fake*>(c); const auto& m = *static_cast<const Method*>(info);
            auto* o = static_cast<Object*>(instance);
            if (m.klass == "Object" && m.name == "FindObjectsOfType") return f.all;
            if (m.klass == "Object" && m.name == "op_Implicit") return f.Prop(static_cast<Object*>(args[0]), "alive");
            if (m.klass == "GameUtil") return f.player;
            if (m.klass == "AnimationClipPlayable") return f.Prop(static_cast<Object*>(args[0]), "clip");
            if (m.klass == "AnimationPlayableOutput") return f.Prop(static_cast<Object*>(args[0]), "target");
            if (!o) { *exception = &f; return nullptr; }
            if (m.klass == "RuntimeType") return o;
            if (m.klass == "Array") {
                if (m.name == "get_Length") return f.Box(static_cast<int>(o->elements.size()));
                const int index = *static_cast<int*>(args[0]);
                if (index < 0 || static_cast<size_t>(index) >= o->elements.size()) { *exception = &f; return nullptr; }
                return o->elements[index];
            }
            if (m.name == "GetInputCount" || m.name == "GetOutputCount") return f.Box(static_cast<int>(o->elements.size()));
            if (m.name == "GetInputHandle" || m.name == "GetOutput") return o->elements.at(*static_cast<int*>(args[0]));
            if (m.name == "GetInputWeight") return f.Box(*static_cast<int*>(args[0]) == 0 ? 0.75f : 0.25f);
            return f.Prop(o, m.name);
        };
        animator = New(); animator->properties = {{"GetInstanceID", Box(42)}, {"get_name", Text("佩丽卡 Animator")},
            {"alive", Box(true)}, {"get_layerCount", Box(1)}, {"get_speed", Box(1.0f)}, {"GetLayerName", Text("Base")},
            {"GetLayerWeight", Box(1.0f)}, {"IsInTransition", Box(false)}};
        auto* state = New(); state->properties = {{"get_fullPathHash", Box(123)}, {"get_normalizedTime", Box(1.5f)}, {"get_length", Box(2.0f)}};
        animator->properties["GetCurrentAnimatorStateInfo"] = state;
        clip = New(); clip->properties = {{"GetInstanceID", Box(77)}, {"get_name", Text("run_loop")}, {"get_length", Box(2.0f)}, {"get_isLooping", Box(true)}};
        auto* clip_info = New(); clip_info->properties = {{"get_clip", clip}, {"get_weight", Box(1.0f)}};
        animator->properties["GetCurrentAnimatorClipInfo"] = Array({clip_info});
        all = Array({animator}); player = New(); player->properties = {{"get_name", Text("Pelica")}, {"GetComponentsInChildren", Array({animator})}};
        auto* graph = Handle(100, ""); graph->properties["IsPlaying"] = Box(true);
        auto* output = New(); auto* output_handle = New(); output->properties["GetHandle"] = output_handle;
        output_handle->properties = {{"GetPlayableOutputType", Text("UnityEngine.Animations.AnimationPlayableOutput")}, {"target", animator}};
        auto* mixer = Handle(101, "UnityEngine.Animations.AnimationMixerPlayable");
        node = Handle(102, "UnityEngine.Animations.AnimationClipPlayable"); node->properties["clip"] = clip;
        mixer->elements = {node, node}; output_handle->properties["GetSourcePlayable"] = mixer;
        graph->elements = {output}; animator->properties["get_playableGraph"] = graph;
    }
};
}
int main() {
    try {
        Fake f; RuntimeReader reader(&f.api);
        Check(reader.Resolve(), "required contracts resolve");
        auto s = reader.Capture(1, 0, true);
        Check(s.status == "valid" && s.character == "Pelica", "follow current player");
        Check(s.layers.size() == 1 && s.layers[0].current_hash == 123, "state fields through managed getters");
        Check(s.clips.size() == 3 && s.nodes.size() == 3, "Animator and two graph paths are preserved");
        Check(!s.clips[0].time && !s.clips[0].speed, "state time never misrepresented as clip time");
        Check(s.clips[1].time == 2.5 && s.clips[1].weight == 0.75 && s.clips[2].weight == 0.25, "local playable time/weights");
        Check(s.clips[1].loop == true && s.graph_id == "100:3", "inherited loop and versioned graph identity");
        Check(s.issues.empty(), "fixture reads are complete");
        Check(reader.Targets().size() == 1 && f.roots.size() == 1, "temporary roots released after capture");
        auto fixed = reader.Capture(2, 42, false);
        Check(fixed.status == "valid" && fixed.mode == "fixed", "fixed selection");
        f.player = nullptr;
        Check(reader.Capture(3, 42, false).status == "valid", "fixed target independent of current player");
        f.animator->properties["alive"] = f.Box(false); f.all->elements.clear();
        auto lost = reader.Capture(4, 42, true);
        Check(lost.status == "target_lost" && lost.clips.empty(), "destroyed target produces no stale clips");
        auto* replacement = f.New(); replacement->properties = f.animator->properties; replacement->properties["alive"] = f.Box(true);
        f.all->elements = {replacement};
        Check(reader.Capture(5, 42, true).status == "target_lost", "fixed root not reacquired by reused ID");
        Check(reader.Capture(6, 0, false).status == "no_target", "no automatic arbitrary fallback");
        reader.Release(); Check(f.roots.empty(), "all managed roots released");
        Fake cycle; RuntimeReader cycle_reader(&cycle.api); Check(cycle_reader.Resolve(), "cycle contracts");
        cycle.node->elements = {cycle.node};
        const auto cyclic = cycle_reader.Capture(0, 0, true);
        Check(cyclic.nodes.size() == 3 && !cyclic.issues.empty(), "graph cycles are bounded");
        cycle_reader.Release();
        Fake missing; missing.unavailable.insert("Animator.GetCurrentAnimatorClipInfo");
        RuntimeReader missing_reader(&missing.api); Check(!missing_reader.Resolve(), "missing core contract rejected");
        std::cout << "Animation debugger reader checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
