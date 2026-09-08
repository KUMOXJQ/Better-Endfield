#include "debug_window.h"
#include <Windows.h>
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

using namespace BetterEndfield::AnimationDebugger;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    DebugChannel channel; channel.enabled = true;
    const auto root = std::filesystem::path(argv[1]);
    bool window_ok = false;
    std::thread ui([&] { window_ok = RunDebugWindow(channel, root, false); });
    int result = 0;
    try {
        for (int i = 0; i < 200 && !channel.window_ready; ++i) Sleep(10);
        if (!channel.window_ready) throw std::runtime_error("hidden window not ready");
        const auto class_name = L"BetterEndfield.AnimationDebugger." + std::to_wstring(GetCurrentProcessId());
        HWND hwnd = FindWindowW(class_name.c_str(), nullptr);
        if (!hwnd || IsWindowVisible(hwnd)) throw std::runtime_error("smoke window must remain hidden");
        SendMessageW(hwnd, WM_COMMAND, 101, 0); // Start
        if (!channel.recording) throw std::runtime_error("record button");
        for (int i = 0; i < 5; ++i) {
            Sample sample; sample.time = ClockSeconds(); sample.status = "valid"; sample.target_id = "fixture";
            sample.character = "SYNTHETIC UI CHECK";
            sample.clips.push_back({"clip", "run", "output/0", "fixture", double(i), 2.0, 1.0, 1.0, true});
            { std::lock_guard lock(channel.mutex); channel.pending.push_back(sample); }
            Sleep(45);
            if (i == 1) {
                SendMessageW(GetDlgItem(hwnd, 104), BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(hwnd, WM_COMMAND, 104, 0); // Freeze must not stop recording
            }
            if (!channel.recording) throw std::runtime_error("freeze stopped recording");
        }
        Sleep(80);
        Sample last; last.time = ClockSeconds(); last.status = "valid"; last.target_id = "fixture";
        last.character = "SYNTHETIC UI CHECK";
        last.clips.push_back({"clip", "run", "output/0", "fixture", 5.0, 2.0, 1.0, 1.0, true});
        { std::lock_guard lock(channel.mutex); channel.pending.push_back(last); }
        SendMessageW(hwnd, WM_COMMAND, 102, 0); // Stop
        if (channel.recording) throw std::runtime_error("stop button");
        if (SendMessageW(GetDlgItem(hwnd, 107), TBM_GETRANGEMAX, 0, 0) != 5)
            throw std::runtime_error("stop must drain queued sample; history needs six samples");
        SendMessageW(hwnd, WM_COMMAND, 101, 0);
        if (channel.recording) throw std::runtime_error("unexported session must be retained");
        SetWindowTextW(GetDlgItem(hwnd, 116), L"absent");
        SendMessageW(hwnd, WM_COMMAND, 119, 0);
        if (IsWindowEnabled(GetDlgItem(hwnd, 107))) throw std::runtime_error("history filter should have no matches");
        SetWindowTextW(GetDlgItem(hwnd, 116), L"fixture");
        SendMessageW(hwnd, WM_COMMAND, 119, 0);
        if (!IsWindowEnabled(GetDlgItem(hwnd, 107))) throw std::runtime_error("history target ID filter");
        SendMessageW(hwnd, WM_COMMAND, 103, 0); // Export
        bool complete = false;
        if (std::filesystem::exists(root)) for (const auto& item : std::filesystem::directory_iterator(root))
            complete |= std::filesystem::exists(item.path() / "COMPLETE.txt");
        if (!complete) throw std::runtime_error("window export");
        std::cout << "Animation debugger hidden window checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    channel.stopping = true; ui.join();
    return window_ok ? result : 1;
}
