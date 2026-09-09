#include "debug_window.h"
#include "sample_scheduler.h"
#include <Windows.h>
#include <algorithm>
#include <memory>
#include <sstream>
#include <shared_mutex>

namespace BetterEndfield::AnimationDebugger {
namespace {
constexpr char ModuleId[] = "betterendfield.animation_debugger";
const BE_HostApiV1* host = nullptr;
DebugChannel channel;
std::unique_ptr<RuntimeReader> reader;
std::thread window_thread;
std::atomic_bool stopping{false};
std::atomic_uint callbacks{0};
using RenderFn = void(BE_CALL*)(const void*);
RenderFn original = nullptr;
RenderFn restored_entry = nullptr;
std::shared_mutex invocation_gate;
SampleScheduler scheduler;
double next_discovery = 0;
uint64_t reported_drops = 0;
std::filesystem::path output_root;

void Capture() {
    const double now = ClockSeconds();
    const int hz = channel.recording ? channel.recording_hz.load() : channel.refresh_hz.load();
    if (!scheduler.Due(now, hz)) return;
    const bool discover = now >= next_discovery;
    Sample sample = reader->Capture(now, channel.target_id.load(), discover);
    sample.capture_ms = (ClockSeconds() - now) * 1000.0;
    if (discover) next_discovery = now + 2.0;
    const uint64_t dropped = channel.dropped.load();
    if (dropped != reported_drops) sample.issues.push_back("Native queue samples dropped: " + std::to_string(dropped - reported_drops));
    std::unique_lock lock(channel.mutex, std::try_to_lock);
    if (!lock || channel.pending.size() >= 120) { ++channel.dropped; return; }
    if (discover) channel.targets = reader->Targets();
    channel.pending.push_back(std::move(sample)); reported_drops = dropped;
}
// Keep SEH in a leaf wrapper, outside functions with C++ stack unwinding.
bool CaptureProtected() {
    __try { Capture(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void BE_CALL RenderHook(const void* method) {
    ++callbacks;
    static thread_local unsigned depth = 0;
    const bool outer = depth++ == 0;
    std::shared_lock invocation(invocation_gate, std::defer_lock);
    if (outer) invocation.lock();
    original(method);
    // Unity can send nested canvas notifications. One capture per interval,
    // with an explicit guard to avoid reentering managed discovery.
    static thread_local bool capturing = false;
    if (outer && !capturing && !stopping && channel.enabled) {
        capturing = true;
        try {
            if (!CaptureProtected()) {
                channel.enabled = false;
                host->log(host->context, ModuleId, "Native read fault; animation debugger disabled");
            }
        } catch (const std::exception& e) {
            channel.enabled = false;
            host->log(host->context, ModuleId, e.what());
        }
        capturing = false;
    }
    --depth;
    --callbacks;
}
BE_Result BE_CALL Initialize(const BE_HostApiV1* api) {
    if (!api || api->abi_version != BETTER_ENDFIELD_MODULE_ABI_V1 || !api->resolve_method || !api->resolve_field
        || !api->resolve_class || !api->runtime_invoke || !api->object_unbox || !api->field_get_value_object
        || !api->gchandle_new || !api->gchandle_free || !api->copy_managed_string || !api->create_hook
        || !api->release_module_hooks || !api->log) return BE_Result_InvalidArgument;
    host = api;
    reader = std::make_unique<RuntimeReader>(host);
    if (!reader->Resolve()) { reader.reset(); return BE_Result_ContractMismatch; }
    BE_MethodDescriptorV1 descriptor{"UnityEngine.UIModule.dll", "UnityEngine", "Canvas",
        "SendWillRenderCanvases", nullptr, "System.Void", 0};
    BE_ResolvedMethodV1 method{};
    if (host->resolve_method(host->context, &descriptor, &method) != BE_Result_Ok) return BE_Result_ContractMismatch;
    // ModuleManager unloads DLLs immediately after shutdown. Pin this DLL so
    // a thread already redirected into the detour cannot resume in freed code.
    // Runtime resources are still released; hot unloading is not supported.
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&RenderHook), &self)) return BE_Result_Failed;
    restored_entry = reinterpret_cast<RenderFn>(method.method_pointer);
    const auto result = host->create_hook(host->context, ModuleId, method.method_pointer,
        reinterpret_cast<void*>(&RenderHook), reinterpret_cast<void**>(&original));
    if (result != BE_Result_Ok) return result;
    wchar_t local[32768]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    if (!size || size >= std::size(local)) { host->release_module_hooks(host->context, ModuleId); return BE_Result_Failed; }
    output_root = std::filesystem::path(local) / "BetterEndfield" / "animation-sessions";
    host->log(host->context, ModuleId, "Animation debugger initialized. Read-only canvas sampling; no animation setters.");
    return BE_Result_Ok;
}
BE_Result BE_CALL ConfigurationChanged(const char* configuration) {
    bool enabled = false;
    std::istringstream input(configuration ? configuration : "");
    std::string line;
    while (std::getline(input, line)) {
        const auto pos = line.find('='); if (pos == std::string::npos) continue;
        auto key = line.substr(0, pos), value = line.substr(pos + 1);
        auto trim = [](std::string& s) {
            const auto first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) { s.clear(); return; }
            s = s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
        };
        trim(key); trim(value);
        if (key == "enabled") enabled = value == "true" || value == "1" || value == "yes";
        try {
            if (key == "sample_hz") channel.sample_hz = std::clamp(std::stoi(value), 1, 120);
            if (key == "refresh_hz") channel.refresh_hz = std::clamp(std::stoi(value), 1, 30);
            if (key == "max_seconds") channel.max_seconds = std::clamp(std::stoi(value), 1, 1800);
            if (key == "max_mib") channel.max_mib = std::clamp(std::stoi(value), 1, 256);
        } catch (...) {}
    }
    channel.enabled = enabled;
    ++channel.configuration_revision;
    if (enabled && !window_thread.joinable()) {
        try {
            window_thread = std::thread([] {
                try {
                    if (RunDebugWindow(channel, output_root)) return;
                    channel.enabled = false;
                    host->log(host->context, ModuleId, "Animation debugger window creation failed");
                } catch (const std::exception& e) {
                    channel.enabled = false;
                    host->log(host->context, ModuleId, e.what());
                }
            });
        } catch (const std::exception& e) {
            channel.enabled = false;
            host->log(host->context, ModuleId, e.what());
            return BE_Result_Failed;
        }
    }
    return BE_Result_Ok;
}
void BE_CALL Shutdown() {
    stopping = true; channel.enabled = false;
    channel.stopping = true;
    if (window_thread.joinable()) window_thread.join();
    {
        // Wait for current invocations, then remove the trampoline while new
        // detour entrants are blocked. Those entrants subsequently call the
        // restored Unity entry instead of the now-freed trampoline.
        std::unique_lock invocation(invocation_gate);
        if (host && host->release_module_hooks(host->context, ModuleId) == BE_Result_Ok)
            original = restored_entry;
    }
    while (callbacks.load()) Sleep(1);
    if (reader) reader->Release();
    reader.reset();
}
const BE_ModuleApiV1 api{{ModuleId, "Animation Debugger", "0.2.0", BETTER_ENDFIELD_MODULE_ABI_V1},
    Initialize, ConfigurationChanged, Shutdown};
}
}
BE_EXPORT const BE_ModuleApiV1* BE_CALL BetterEndfield_GetModuleApiV1() {
    return &BetterEndfield::AnimationDebugger::api;
}
