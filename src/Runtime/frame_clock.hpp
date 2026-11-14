#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <windows.h>

namespace gengine
{
    // 待機だけをworkerへ任せ、ゲームとGPUの操作はHWNDの所有スレッドで行う。
    // 未処理の通知は一件にまとめ、重いフレームの後へ描画要求を蓄積しない。
    class FrameClock final
    {
      public:
        static constexpr auto kMessage = WM_APP + 0x240;
        ~FrameClock();
        void Start(HWND window);
        void Stop();
        std::uint64_t Consume(std::uint64_t scheduled_ns);
        bool running() const;
        bool high_resolution() const;
        std::uint64_t coalesced() const;

      private:
        void Run();
        void Post(std::uint64_t scheduled_ns);
        HWND window_ = nullptr;
        HANDLE stop_ = nullptr;
        HANDLE timer_ = nullptr;
        std::thread thread_;
        std::atomic<bool> pending_ = false;
        std::atomic<DWORD> error_ = ERROR_SUCCESS;
        std::atomic<std::uint64_t> coalesced_ = 0;
        bool high_resolution_ = false;
    };
} // namespace gengine
