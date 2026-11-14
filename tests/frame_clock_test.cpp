#include "frame_clock.hpp"
#include "performance.hpp"
#include "presentation_timing.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    void Require(bool condition, const char* reason)
    {
        if (condition == false) {
            throw std::runtime_error(reason);
        }
    }
} // namespace

int main()
{
    try {
        auto wc = WNDCLASSW{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"gEngineFrameClockTest";
        Require(RegisterClassW(&wc) != 0, "register message window");
        const auto window =
            CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        Require(window != nullptr, "create message window");
        {
            // 提示の欠測や不連続を、連続フレームの短い間隔として数えない。
            const auto first = rengine::PresentationTiming{1, 100, 10, 100, 100, 1000000, 10000000, 1, 0};
            auto next = first;
            ++next.sequence;
            ++next.present_count;
            next.present_refresh_count += 3;
            next.sync_refresh_count += 3;
            next.sync_qpc += 180000;
            Require(gengine::PresentationInterval(first, next) == 18000000, "presentation interval incorrect");
            auto invalid = next;
            invalid.available = 0;
            Require(gengine::PresentationInterval(first, invalid).has_value() == false,
                    "missing presentation accepted");
            invalid = next;
            invalid.disjoint = 1;
            Require(gengine::PresentationInterval(first, invalid).has_value() == false,
                    "disjoint presentation accepted");
            invalid = next;
            ++invalid.present_count;
            Require(gengine::PresentationInterval(first, invalid).has_value() == false, "missing frame averaged");
            invalid = next;
            ++invalid.sync_refresh_count;
            Require(gengine::PresentationInterval(first, invalid).has_value() == false, "unmatched refresh accepted");
            Require(gengine::PresentationInterval(first, first).has_value() == false, "duplicate presentation counted");

            auto clock = gengine::FrameClock{};
            try {
                clock.Start(nullptr);
                throw std::runtime_error("null window accepted");
            } catch (const std::invalid_argument&) {
            }

            // UIが処理できない間は一件だけ残し、再開時に古い描画を連続実行しない。
            clock.Start(window);
            clock.Start(window);
            Sleep(160);
            auto message = MSG{};
            Require(PeekMessageW(&message, window, gengine::FrameClock::kMessage, gengine::FrameClock::kMessage,
                                 PM_REMOVE) != FALSE,
                    "first frame missing");
            const auto scheduled = static_cast<std::uint64_t>(message.lParam);
            Require(PeekMessageW(&message, window, gengine::FrameClock::kMessage, gengine::FrameClock::kMessage,
                                 PM_NOREMOVE) == FALSE,
                    "frame queue accumulated");
            Require(clock.coalesced() >= 3, "blocked UI was not coalesced");
            Require(clock.Consume(scheduled) > 20000000, "queue delay not measured");
            clock.Stop();
            Sleep(30);
            Require(PeekMessageW(&message, window, gengine::FrameClock::kMessage, gengine::FrameClock::kMessage,
                                 PM_REMOVE) == FALSE,
                    "notification survived stop");

            // 実際の待機とメッセージ配送を通し、固定周期と再開後の応答を計測する。
            clock.Start(window);
            auto intervals = std::vector<std::uint64_t>{};
            auto lateness = std::vector<std::uint64_t>{};
            auto previous = std::chrono::steady_clock::now();
            const auto deadline = previous + std::chrono::seconds(5);
            while (intervals.size() < 120) {
                Require(std::chrono::steady_clock::now() < deadline, "frame clock timed out");
                MsgWaitForMultipleObjectsEx(0, nullptr, 200, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE);
                if (PeekMessageW(&message, window, gengine::FrameClock::kMessage, gengine::FrameClock::kMessage,
                                 PM_REMOVE)) {
                    const auto now = std::chrono::steady_clock::now();
                    intervals.push_back(static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous).count()));
                    lateness.push_back(clock.Consume(static_cast<std::uint64_t>(message.lParam)));
                    previous = now;
                }
            }
            const auto interval = gengine::Summarize("interval", intervals);
            const auto delay = gengine::Summarize("scheduler_lateness", lateness);
            Require(interval.mean_ns > 14000000 && interval.mean_ns < 22000000, "frame cadence drifted");
            std::cout << "high_resolution=" << clock.high_resolution() << " mean_ns=" << interval.mean_ns
                      << " interval_p95_ns=" << interval.p95_ns << " lateness_p95_ns=" << delay.p95_ns << '\n';
            clock.Stop();

            // 各再開で作るthread・event・timerを停止時に回収する。
            const auto before = gengine::ReadProcessResources();
            for (auto cycle = 0; cycle < 32; ++cycle) {
                clock.Start(window);
                Sleep(1);
                clock.Stop();
            }
            const auto after = gengine::ReadProcessResources();
            Require(after.handles == before.handles && after.user_objects == before.user_objects &&
                        after.gdi_objects == before.gdi_objects,
                    "frame clock resources retained");
            clock.Start(window);
            PostQuitMessage(17);
            clock.Stop();
            Require(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE &&
                        message.message == WM_QUIT && message.wParam == 17,
                    "frame clock swallowed host quit request");
        }
        DestroyWindow(window);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
