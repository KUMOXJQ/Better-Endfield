#include "debug_window.h"
#include <Windows.h>
#include <commctrl.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace BetterEndfield::AnimationDebugger {
double ClockSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string UtcNow() {
    SYSTEMTIME time{}; GetSystemTime(&time);
    char text[64]{};
    std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear, time.wMonth,
        time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return text;
}
namespace {
std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::wstring ReadText(HWND control) {
    const int size = GetWindowTextLengthW(control);
    std::wstring result(static_cast<size_t>(size) + 1, L'\0');
    GetWindowTextW(control, result.data(), size + 1); result.resize(size); return result;
}
enum Id { TargetBox = 100, Record, Stop, ExportFiles, Freeze, Graph, Filter, History,
    SampleHz, RefreshHz, MaxSeconds, MaxMb, EventList, Status, Body, Live,
    HistoryTarget, TimeFrom, TimeTo, ApplyHistory };
class Window {
public:
    DebugChannel& channel;
    std::filesystem::path root;
    HWND hwnd = nullptr;
    std::map<int, HWND> controls;
    Session session;
    Sample latest, frozen;
    std::vector<Target> targets;
    std::vector<size_t> history_indices;
    bool freeze = false, graph = false, history = false;
    bool exported = true;
    double start = 0, last_received = 0, last_render = 0;
    const double display_origin = ClockSeconds();
    size_t event_count = 0;
    uint64_t starting_drops = 0;
    unsigned configuration_revision = 0;
    std::wstring notice = L"就绪。Ctrl+Alt+F8 最小化/恢复。选择目标后开始记录。";
    HFONT font = nullptr;
    bool visible = true;
    explicit Window(DebugChannel& c, std::filesystem::path p) : channel(c), root(std::move(p)) {}
    HWND Add(int id, const wchar_t* klass, const wchar_t* text, DWORD style, int x, int y, int w, int h) {
        HWND control = CreateWindowExW(klass == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
            klass, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!control) throw std::runtime_error("Cannot create debugger control");
        controls[id] = control; SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return control;
    }
    void CreateControls() {
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        Add(TargetBox, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 10, 10, 405, 400);
        SendMessageW(controls[TargetBox], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"跟随当前角色"));
        SendMessageW(controls[TargetBox], CB_SETCURSEL, 0, 0);
        Add(Record, L"BUTTON", L"开始记录", BS_PUSHBUTTON, 425, 10, 95, 28);
        Add(Stop, L"BUTTON", L"停止", BS_PUSHBUTTON, 525, 10, 75, 28);
        Add(ExportFiles, L"BUTTON", L"导出 JSON/CSV", BS_PUSHBUTTON, 605, 10, 140, 28);
        Add(Freeze, L"BUTTON", L"冻结显示", BS_AUTOCHECKBOX, 10, 48, 110, 25);
        Add(Graph, L"BUTTON", L"展开图", BS_AUTOCHECKBOX, 125, 48, 95, 25);
        Add(999, L"STATIC", L"Clip 筛选", 0, 225, 51, 80, 23);
        Add(Filter, L"EDIT", L"", ES_AUTOHSCROLL, 310, 47, 210, 26);
        Add(Live, L"BUTTON", L"返回实时", BS_PUSHBUTTON, 530, 47, 95, 28);
        Add(998, L"STATIC", L"采样 Hz / 刷新 Hz / 时长秒 / 容量 MiB", 0, 10, 87, 335, 25);
        Add(SampleHz, L"EDIT", std::to_wstring(channel.sample_hz.load()).c_str(), ES_NUMBER, 350, 82, 62, 26);
        Add(RefreshHz, L"EDIT", std::to_wstring(channel.refresh_hz.load()).c_str(), ES_NUMBER, 420, 82, 62, 26);
        Add(MaxSeconds, L"EDIT", std::to_wstring(channel.max_seconds.load()).c_str(), ES_NUMBER, 490, 82, 88, 26);
        Add(MaxMb, L"EDIT", std::to_wstring(channel.max_mib.load()).c_str(), ES_NUMBER, 585, 82, 62, 26);
        Add(997, L"STATIC", L"历史样本（停止后拖动，左右键逐条）", 0, 10, 119, 345, 25);
        Add(History, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS, 350, 114, 395, 30);
        Add(996, L"STATIC", L"历史角色/Animator/ID", 0, 10, 157, 180, 25);
        Add(HistoryTarget, L"EDIT", L"", ES_AUTOHSCROLL, 195, 151, 170, 26);
        Add(995, L"STATIC", L"起止秒", 0, 375, 157, 65, 25);
        Add(TimeFrom, L"EDIT", L"", ES_AUTOHSCROLL, 445, 151, 70, 26);
        Add(TimeTo, L"EDIT", L"", ES_AUTOHSCROLL, 523, 151, 70, 26);
        Add(ApplyHistory, L"BUTTON", L"筛选历史", BS_PUSHBUTTON, 605, 151, 115, 28);
        Add(Status, L"STATIC", L"", 0, 10, 187, 850, 65);
        Add(Body, L"EDIT", L"等待 Unity 主线程样本…", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL, 10, 256, 900, 286);
        Add(EventList, L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT, 10, 550, 900, 130);
        SendMessageW(controls[Body], EM_SETLIMITTEXT, 1024 * 1024, 0);
        EnableWindow(controls[History], FALSE);
        if (visible && !RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F8)) notice = L"Ctrl+Alt+F8 被占用；可使用任务栏恢复窗口。";
        if (!SetTimer(hwnd, 1, 25, nullptr)) throw std::runtime_error("Cannot start debugger timer");
        channel.window_ready = true;
        configuration_revision = channel.configuration_revision.load();
    }
    void Layout() {
        RECT rect{}; GetClientRect(hwnd, &rect);
        const int width = std::max(750L, rect.right) - 20;
        const int body_height = std::max(120L, rect.bottom - 424);
        MoveWindow(controls[Status], 10, 187, width, 65, TRUE);
        MoveWindow(controls[Body], 10, 256, width, body_height, TRUE);
        MoveWindow(controls[EventList], 10, 264 + body_height, width, 150, TRUE);
    }
    int Setting(int id, int fallback, int low, int high) {
        try { return std::clamp(std::stoi(ReadText(controls[id])), low, high); } catch (...) { return fallback; }
    }
    void Begin() {
        if (session.recording || !channel.enabled.load()) return;
        if (!exported && !session.id.empty()) {
            notice = L"上一会话尚未导出，请先导出再开始新会话。"; return;
        }
        channel.sample_hz = Setting(SampleHz, 30, 1, 120);
        start = ClockSeconds();
        SYSTEMTIME t{}; GetSystemTime(&t); char id[96]{};
        std::snprintf(id, sizeof(id), "animation-%04u%02u%02u-%02u%02u%02u-%03u-%lu", t.wYear, t.wMonth, t.wDay,
            t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId());
        session.Start(id, UtcNow(), channel.sample_hz, Setting(MaxSeconds, 1800, 1, 1800),
            static_cast<size_t>(Setting(MaxMb, 64, 1, 256)) * 1024 * 1024);
        session.initial_mode = channel.target_id ? "fixed" : "follow";
        session.initial_target_id = channel.target_id ? std::to_string(channel.target_id.load()) : "";
        session.game_version = latest.game_version;
        session.refresh_hz = channel.refresh_hz;
        channel.recording_hz = session.sample_hz;
        channel.recording = true; exported = false; history = false; event_count = 0;
        starting_drops = channel.dropped.load();
        SendMessageW(controls[EventList], LB_RESETCONTENT, 0, 0);
        EnableWindow(controls[History], FALSE);
        EnableWindow(controls[SampleHz], FALSE);
        notice = L"正在记录；冻结或最小化窗口不会停止记录。";
    }
    void End(const char* reason) {
        const bool was_recording = session.recording || channel.recording;
        if (session.recording) ConsumePending();
        if (was_recording && channel.dropped.load() > starting_drops)
            session.events.push_back({ClockSeconds() - start, "queue_drop_total", std::to_string(channel.dropped.load() - starting_drops)});
        session.Stop(reason, ClockSeconds() - start, UtcNow()); channel.recording = false;
        EnableWindow(controls[SampleHz], TRUE);
        if (was_recording) BuildHistory();
    }
    void BuildHistory() {
        if (session.recording) return;
        const auto target = Utf8(ReadText(controls[HistoryTarget]));
        const auto clip = Utf8(ReadText(controls[Filter]));
        double from = 0, to = session.max_seconds;
        try {
            const auto a = ReadText(controls[TimeFrom]), b = ReadText(controls[TimeTo]);
            if (!a.empty()) from = std::stod(a);
            if (!b.empty()) to = std::stod(b);
            if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || from > to) throw std::runtime_error("range");
        } catch (...) { notice = L"历史时间范围无效；请输入起止秒，或留空。"; return; }
        history_indices.clear();
        for (size_t i = 0; i < session.samples.size(); ++i) {
            const auto& s = session.samples[i];
            if (s.time < from || s.time > to) continue;
            if (!target.empty() && s.character.find(target) == std::string::npos && s.animator.find(target) == std::string::npos && s.target_id.find(target) == std::string::npos) continue;
            if (!clip.empty() && std::none_of(s.clips.begin(), s.clips.end(), [&](const Clip& c) { return c.name.find(clip) != std::string::npos; })) continue;
            history_indices.push_back(i);
        }
        EnableWindow(controls[History], !history_indices.empty());
        SendMessageW(controls[History], TBM_SETRANGEMIN, FALSE, 0);
        SendMessageW(controls[History], TBM_SETRANGEMAX, TRUE, history_indices.empty() ? 0 : history_indices.size() - 1);
        notice = L"历史筛选匹配 " + std::to_wstring(history_indices.size()) + L" 个样本。导出保留完整会话。";
        if (!history_indices.empty()) SetHistory(history_indices.front());
    }
    void DoExport() {
        if (session.recording || session.id.empty()) { notice = L"请先停止记录。"; return; }
        try {
            const auto directory = Export(session, root);
            notice = L"导出成功：" + directory.wstring(); exported = true;
        } catch (const std::exception& e) { notice = L"导出失败，数据仍保留，可重试：" + Wide(e.what()); }
    }
    void SetHistory(size_t index) {
        if (session.recording || session.samples.empty()) return;
        index = std::min(index, session.samples.size() - 1);
        history = true; frozen = session.samples[index];
        const auto it = std::find(history_indices.begin(), history_indices.end(), index);
        if (it != history_indices.end()) SendMessageW(controls[History], TBM_SETPOS, TRUE, it - history_indices.begin());
        last_render = 0;
    }
    void Command(int id, int notification) {
        if (id == TargetBox && notification == CBN_SELCHANGE) {
            const auto index = SendMessageW(controls[TargetBox], CB_GETCURSEL, 0, 0);
            channel.target_id = index > 0 && static_cast<size_t>(index) <= targets.size() ? targets[index - 1].id : 0;
        } else if (id == Record) Begin();
        else if (id == Stop) End("user");
        else if (id == ExportFiles) DoExport();
        else if (id == ApplyHistory) BuildHistory();
        else if (id == Freeze) {
            freeze = SendMessageW(controls[Freeze], BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (freeze) frozen = latest;
            history = false;
        } else if (id == Graph) graph = SendMessageW(controls[Graph], BM_GETCHECK, 0, 0) == BST_CHECKED;
        else if (id == Live) { history = false; freeze = false; SendMessageW(controls[Freeze], BM_SETCHECK, BST_UNCHECKED, 0); }
        else if (id == EventList && notification == LBN_SELCHANGE && !session.recording) {
            const auto index = SendMessageW(controls[EventList], LB_GETCURSEL, 0, 0);
            if (index >= 0) {
                const auto event_index = SendMessageW(controls[EventList], LB_GETITEMDATA, index, 0);
                if (event_index >= 0 && static_cast<size_t>(event_index) < session.events.size()) {
                    const double time = session.events[event_index].time;
                    const auto it = std::lower_bound(session.samples.begin(), session.samples.end(), time,
                        [](const Sample& sample, double t) { return sample.time < t; });
                    SetHistory(static_cast<size_t>(it - session.samples.begin()));
                }
            }
        }
        last_render = 0;
    }
    void UpdateTargets(std::vector<Target> list) {
        bool changed = list.size() != targets.size();
        for (size_t i = 0; !changed && i < list.size(); ++i) changed = list[i].id != targets[i].id || list[i].label != targets[i].label;
        if (!changed || SendMessageW(controls[TargetBox], CB_GETDROPPEDSTATE, 0, 0)) return;
        const int selected = channel.target_id.load();
        // Retain a disappeared fixed target in the selector. Never silently
        // retarget a recording because discovery no longer lists that object.
        if (selected && std::none_of(list.begin(), list.end(), [&](const Target& t) { return t.id == selected; }))
            list.push_back({selected, "Unavailable [" + std::to_string(selected) + "]"});
        targets = std::move(list);
        SendMessageW(controls[TargetBox], CB_RESETCONTENT, 0, 0);
        SendMessageW(controls[TargetBox], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"跟随当前角色"));
        int selection = 0;
        for (size_t i = 0; i < targets.size(); ++i) {
            const auto label = Wide(targets[i].label);
            SendMessageW(controls[TargetBox], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (targets[i].id == selected) selection = static_cast<int>(i + 1);
        }
        SendMessageW(controls[TargetBox], CB_SETCURSEL, selection, 0);
    }
    void ConsumePending() {
        std::deque<Sample> pending;
        std::vector<Target> available;
        { std::lock_guard lock(channel.mutex); pending.swap(channel.pending); available = channel.targets; }
        UpdateTargets(std::move(available));
        for (auto& sample : pending) {
            latest = sample; last_received = sample.time;
            if (session.recording && sample.time >= start) {
                sample.time -= start; session.Append(std::move(sample), UtcNow());
            }
        }
    }
    void Tick() {
        const double now = ClockSeconds();
        if (configuration_revision != channel.configuration_revision.load()) {
            configuration_revision = channel.configuration_revision.load();
            SetWindowTextW(controls[SampleHz], std::to_wstring(channel.sample_hz.load()).c_str());
            SetWindowTextW(controls[RefreshHz], std::to_wstring(channel.refresh_hz.load()).c_str());
            SetWindowTextW(controls[MaxSeconds], std::to_wstring(channel.max_seconds.load()).c_str());
            SetWindowTextW(controls[MaxMb], std::to_wstring(channel.max_mib.load()).c_str());
        }
        ConsumePending();
        if (session.recording && now - std::max(last_received, start) > 0.5) {
            Sample gap; gap.time = now - start; gap.mode = channel.target_id ? "fixed" : "follow";
            gap.target_id = latest.target_id; gap.status = "no_main_thread_samples";
            gap.issues.push_back("No Unity callback for over 0.5 seconds; data not carried forward");
            if (session.samples.empty() || gap.time - session.samples.back().time >= 0.5) session.Append(std::move(gap), UtcNow());
        }
        if (session.recording && !channel.enabled) End("module_disabled");
        if (session.recording && now - start >= session.max_seconds) End("duration_limit");
        if (channel.recording && !session.recording) {
            End(session.stop_reason.c_str()); notice = L"记录已自动停止：" + Wide(session.stop_reason);
        }
        channel.refresh_hz = Setting(RefreshHz, 10, 1, 30);
        if (!session.recording) channel.sample_hz = Setting(SampleHz, 30, 1, 120);
        EnableWindow(controls[Record], channel.enabled && !session.recording);
        EnableWindow(controls[Stop], session.recording);
        EnableWindow(controls[ExportFiles], !session.recording && !session.id.empty());
        if (now - last_render < 1.0 / channel.refresh_hz.load()) return;
        last_render = now;
        const bool stale = !last_received || now - last_received > 0.5;
        const std::wstring title = session.recording ? L"● REC - Better Endfield 动画调试（最小化后继续记录）" : L"Better Endfield 动画调试";
        SetWindowTextW(hwnd, title.c_str());
        const std::wstring state = session.recording ? L"记录中" : L"已停止";
        const std::wstring status = state + L" | 样本 " + std::to_wstring(session.samples.size())
            + L" | 秒 " + std::to_wstring(session.recording ? now - start : session.samples.empty() ? 0.0 : session.samples.back().time)
            + L" | 估算 MiB " + std::to_wstring(session.estimated_bytes / (1024 * 1024))
            + L" | 队列丢弃 " + std::to_wstring(channel.dropped.load())
            + L" | 异常 " + std::to_wstring(session.issue_count)
            + L" | 实际 Hz " + std::to_wstring(session.ActualSampleHz()).substr(0, 5)
            + L" | " + (history ? L"历史样本" : freeze ? L"显示已冻结" : stale ? L"数据过期" : L"实时")
            + L"\n" + notice;
        SetWindowTextW(controls[Status], status.c_str());
        Sample view = (history || freeze) ? frozen : latest;
        if (!history && !freeze && stale) {
            view.status = "stale"; view.clips.clear(); view.layers.clear(); view.nodes.clear(); view.animator_speed.reset();
            view.graph_status = "stale"; view.issues.push_back("Waiting for Unity main-thread callback");
        }
        if (!history) view.time = std::max(0.0, view.time - (session.recording ? start : display_origin));
        const auto body = Wide(Describe(view, Utf8(ReadText(controls[Filter])), graph));
        // Preserve the reader's scroll position while refreshing the text.
        const auto line = SendMessageW(controls[Body], EM_GETFIRSTVISIBLELINE, 0, 0);
        SetWindowTextW(controls[Body], body.c_str());
        SendMessageW(controls[Body], EM_LINESCROLL, 0, line);
        if (event_count != session.events.size()) {
            SendMessageW(controls[EventList], WM_SETREDRAW, FALSE, 0);
            SendMessageW(controls[EventList], LB_RESETCONTENT, 0, 0);
            const size_t begin = session.events.size() > 500 ? session.events.size() - 500 : 0;
            for (size_t i = begin; i < session.events.size(); ++i) {
                const auto& e = session.events[i];
                const auto text = Wide(std::to_string(e.time) + " | " + e.kind + " | " + e.detail);
                auto index = SendMessageW(controls[EventList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
                if (index >= 0) SendMessageW(controls[EventList], LB_SETITEMDATA, index, i);
            }
            SendMessageW(controls[EventList], WM_SETREDRAW, TRUE, 0);
            InvalidateRect(controls[EventList], nullptr, TRUE); event_count = session.events.size();
        }
    }
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->hwnd = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        try {
            switch (message) {
            case WM_CREATE: self->CreateControls(); return 0;
            case WM_SIZE: self->Layout(); return 0;
            case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {790, 610}; return 0;
            case WM_COMMAND: self->Command(LOWORD(w), HIWORD(w)); return 0;
            case WM_HSCROLL: if (reinterpret_cast<HWND>(l) == self->controls[History]) {
                const auto index = static_cast<size_t>(SendMessageW(self->controls[History], TBM_GETPOS, 0, 0));
                if (index < self->history_indices.size()) self->SetHistory(self->history_indices[index]);
                } return 0;
            case WM_HOTKEY: ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_MINIMIZE); return 0;
            case WM_CLOSE: ShowWindow(hwnd, SW_MINIMIZE); return 0;
            case WM_TIMER:
                self->Tick();
                if (self->channel.stopping) {
                    self->End("shutdown");
                    if (!self->exported && !self->session.id.empty()) self->DoExport();
                    DestroyWindow(hwnd);
                }
                return 0;
            case WM_DESTROY: UnregisterHotKey(hwnd, 1); KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
            }
        } catch (const std::exception& e) {
            self->notice = L"调试窗口错误：" + Wide(e.what());
            self->End("window_error");
            if (message == WM_CREATE) return -1;
        }
        return DefWindowProcW(hwnd, message, w, l);
    }
};
}
bool RunDebugWindow(DebugChannel& channel, const std::filesystem::path& output_root, bool visible) {
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES}; InitCommonControlsEx(&common);
    Window window(channel, output_root);
    window.visible = visible;
    const std::wstring class_name = L"BetterEndfield.AnimationDebugger." + std::to_wstring(GetCurrentProcessId());
    WNDCLASSW cls{}; cls.lpfnWndProc = Window::Proc; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = class_name.c_str(); cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassW(&cls)) return false;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, class_name.c_str(), L"Better Endfield 动画调试", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 800, nullptr, nullptr, cls.hInstance, &window);
    if (hwnd) {
        if (visible) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (window.font) DeleteObject(window.font);
    UnregisterClassW(class_name.c_str(), cls.hInstance);
    channel.window_ready = false;
    return hwnd != nullptr;
}
}
