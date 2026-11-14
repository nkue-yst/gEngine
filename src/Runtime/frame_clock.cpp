#include "frame_clock.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <system_error>

namespace gengine
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto kPeriod = std::chrono::nanoseconds(1000000000 / 60);

        std::uint64_t Nanoseconds(Clock::time_point time)
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count());
        }
    } // namespace

    FrameClock::~FrameClock()
    {
        Stop();
    }

    void FrameClock::Start(HWND window)
    {
        if (GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) {
            throw std::invalid_argument("frame clock requires a window owned by the calling thread");
        }
        if (running()) {
            if (window != window_) {
                throw std::logic_error("frame clock already belongs to another window");
            }
            return;
        }

        // 作成失敗でも確保済みのハンドルを回収し、部分的に動いた時計を残さない。
        window_ = window;
        error_ = ERROR_SUCCESS;
        coalesced_ = 0;
        try {
            stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (stop_ == nullptr) {
                throw std::system_error(GetLastError(), std::system_category(), "create frame stop event");
            }
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                            TIMER_MODIFY_STATE | SYNCHRONIZE);
            high_resolution_ = timer_ != nullptr;
            // 古いWindowsでも起動は維持する。実際に使えた時計の種類は計測値で区別する。
            if (timer_ == nullptr && GetLastError() == ERROR_INVALID_PARAMETER) {
                timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
            }
            if (timer_ == nullptr) {
                throw std::system_error(GetLastError(), std::system_category(), "create frame timer");
            }
            thread_ = std::thread([this] {
                Run();
            });
        } catch (...) {
            Stop();
            throw;
        }
    }

    void FrameClock::Stop()
    {
        // workerはUIへ同期呼出ししないため、終了を待ってからHWNDと通知を安全に解放できる。
        if (stop_ != nullptr) {
            SetEvent(stop_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (timer_ != nullptr) {
            CancelWaitableTimer(timer_);
            CloseHandle(timer_);
            timer_ = nullptr;
        }
        if (stop_ != nullptr) {
            CloseHandle(stop_);
            stop_ = nullptr;
        }
        if (window_ != nullptr) {
            auto message = MSG{};
            while (PeekMessageW(&message, window_, kMessage, kMessage, PM_REMOVE)) {
                // WM_QUITはフィルターに関係なく返るため、ホストの終了要求を取り除かない。
                if (message.message == WM_QUIT) {
                    PostQuitMessage(static_cast<int>(message.wParam));
                    break;
                }
            }
            window_ = nullptr;
        }
        pending_ = false;
    }

    void FrameClock::Post(std::uint64_t scheduled_ns)
    {
        if (pending_.exchange(true)) {
            ++coalesced_;
            return;
        }
        if (PostMessageW(window_, kMessage, 0, static_cast<LPARAM>(scheduled_ns)) == FALSE) {
            error_ = GetLastError();
            pending_ = false;
        }
    }

    void FrameClock::Run()
    {
        const auto handles = std::array{stop_, timer_};
        auto next = Clock::now() + kPeriod;
        while (true) {
            auto due = LARGE_INTEGER{};
            const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(next - Clock::now()).count();
            due.QuadPart = -std::max<std::int64_t>(1, (remaining + 99) / 100);
            if (SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0) == FALSE) {
                error_ = GetLastError();
                Post(Nanoseconds(next));
                return;
            }
            const auto waited =
                WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
            if (waited == WAIT_OBJECT_0) {
                return;
            }
            if (waited != WAIT_OBJECT_0 + 1) {
                error_ = waited == WAIT_FAILED ? GetLastError() : ERROR_INVALID_FUNCTION;
                Post(Nanoseconds(next));
                return;
            }

            Post(Nanoseconds(next));
            // 絶対的な予定を進めて誤差を累積させない。休止からの復帰では過去の予定を飛ばす。
            next += kPeriod;
            const auto now = Clock::now();
            if (next <= now) {
                next += kPeriod * ((now - next) / kPeriod + 1);
            }
        }
    }

    std::uint64_t FrameClock::Consume(std::uint64_t scheduled_ns)
    {
        pending_ = false;
        if (const auto error = error_.load(); error != ERROR_SUCCESS) {
            throw std::system_error(error, std::system_category(), "frame timer");
        }
        const auto now = Nanoseconds(Clock::now());
        return now > scheduled_ns ? now - scheduled_ns : 0;
    }

    bool FrameClock::running() const
    {
        return thread_.joinable();
    }

    bool FrameClock::high_resolution() const
    {
        return high_resolution_;
    }

    std::uint64_t FrameClock::coalesced() const
    {
        return coalesced_.load();
    }
} // namespace gengine
