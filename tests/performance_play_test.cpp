#include "play_window.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <thread>

namespace
{
    void Require(bool condition, const char* reason)
    {
        if (condition == false) {
            throw std::runtime_error(reason);
        }
    }
    void Pump()
    {
        MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        auto message = MSG{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        {
            Require(argc == 4, "content output 2d|3d required");
            const auto three = std::wstring(argv[3]) == L"3d";
            const auto source = gengine::AssetSource(argv[1]);
            const auto input = gengine::LoadGameInput(source, three);
            auto executable = std::array<wchar_t, 32768>{};
            GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            auto window = gengine::PlayWindow(
                nullptr, input.module, std::filesystem::path(executable.data()).parent_path() / "backends",
                three ? gengine::GameMode::k3d : gengine::GameMode::k2d, input.scene, false, &source);
            auto rect = RECT{0, 0, 960, 540};
            AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, FALSE);
            SetWindowPos(window.window(), nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOMOVE | SWP_NOZORDER);
            const auto directory = std::filesystem::path(argv[2]);
            std::filesystem::create_directories(directory);
            for (const auto preset : {0u, 4u}) {
                window.Reset(preset);
                if (preset == 0) {
                    SendMessageW(window.window(), WM_KEYDOWN, VK_RETURN, 0);
                    SendMessageW(window.window(), WM_KEYUP, VK_RETURN, 0);
                }
                window.StartPerformance({30, 300});
                try {
                    window.Capture();
                    throw std::runtime_error("image capture contaminated timing");
                } catch (const std::logic_error&) {
                }
                auto frame = window.metrics().frame;
                auto frame_since_reset = 0u;
                auto right = true;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
                while (window.PerformanceActive()) {
                    Require(std::chrono::steady_clock::now() < deadline, "capture timed out");
                    const auto& state = window.game().state();
                    Require(state.phase != gengine::Phase::kFaulted, state.diagnostic.c_str());
                    // 通常入力は移動と射撃。負荷条件は既存プリセットの初期4フレームを繰り返す。
                    if (preset == 0) {
                        if (state.player.x > 1.5f) {
                            right = false;
                        }
                        if (state.player.x < -1.5f) {
                            right = true;
                        }
                        SendMessageW(window.window(), WM_KEYDOWN, VK_SPACE, 0);
                        SendMessageW(window.window(), right ? WM_KEYDOWN : WM_KEYUP, 'D', 0);
                        SendMessageW(window.window(), right ? WM_KEYUP : WM_KEYDOWN, 'A', 0);
                    }
                    Pump();
                    if (window.metrics().frame != frame) {
                        frame = window.metrics().frame;
                        if (preset == 4 && ++frame_since_reset == 4 && window.PerformanceActive()) {
                            window.Reset(4);
                            frame_since_reset = 0;
                        }
                    }
                }
                const auto report = window.Performance();
                Require(report.collected == 300 && report.timings[7].samples > 100, "missing CPU/GPU samples");
                Require(report.process_end.private_bytes < 256ull * 1024 * 1024, "standalone memory budget");
                std::ofstream(directory / (preset == 0 ? "normal.json" : "load.json"))
                    << gengine::PerformanceJson(report);
                std::cout << (preset == 0 ? "normal " : "load ") << gengine::PerformanceJson(report) << '\n';
            }

            // 最小化中は時計と停止入力を保ち、GPU描画や直前のGPU標本を再計上しない。
            window.Reset(0);
            ShowWindow(window.window(), SW_MINIMIZE);
            Require(IsIconic(window.window()) != FALSE, "window did not minimize");
            window.StartPerformance({3, 12});
            const auto minimized_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (window.PerformanceActive()) {
                Require(std::chrono::steady_clock::now() < minimized_deadline, "minimized timer stopped");
                Pump();
            }
            const auto minimized = window.Performance();
            Require(minimized.collected == 12 && minimized.timings[6].max_ns == 0 &&
                        minimized.timings[7].samples == 0 && minimized.timings[12].samples == 0,
                    "minimized rendering or stale timing");
            ShowWindow(window.window(), SW_RESTORE);
            window.StartPerformance({3, 12});
            const auto restored_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (window.PerformanceActive()) {
                Require(std::chrono::steady_clock::now() < restored_deadline, "restored timer stopped");
                Pump();
            }
            const auto restored = window.Performance();
            Require(restored.timings[6].max_ns > 0 && restored.timings[7].samples > 0 &&
                        restored.timings[12].samples == 12,
                    "presentation did not resume after restore");
            std::cout << "minimized wait, rendering suspension and restored presentation passed\n";
        }
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        CoUninitialize();
        return 1;
    }
}
