#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gengine
{
    struct TimingSummary
    {
        std::string name;
        std::uint64_t samples = 0;
        double mean_ns = 0;
        std::uint64_t p50_ns = 0;
        std::uint64_t p95_ns = 0;
        std::uint64_t p99_ns = 0;
        std::uint64_t max_ns = 0;
    };
    struct PerformanceCounter
    {
        std::string name;
        std::uint64_t value = 0;
    };
    struct ProcessResources
    {
        std::uint64_t private_bytes = 0;
        std::uint64_t working_set_bytes = 0;
        std::uint64_t peak_working_set_bytes = 0;
        std::uint64_t cpu_ns = 0;
        std::uint32_t handles = 0;
        std::uint32_t gdi_objects = 0;
        std::uint32_t user_objects = 0;
    };
    struct PerformanceConfig
    {
        std::uint32_t warmup = 30;
        std::uint32_t samples = 300;
    };
    struct PerformanceReport
    {
        std::string source;
        std::string game;
        std::string state = "Idle";
        PerformanceConfig config;
        std::uint32_t warmed = 0;
        std::uint32_t collected = 0;
        std::uint64_t elapsed_ns = 0;
        std::vector<TimingSummary> timings;
        std::vector<PerformanceCounter> counters;
        ProcessResources process_begin;
        ProcessResources process_end;
    };
    enum class Timing : std::size_t
    {
        kFrame,
        kInterval,
        kVm,
        kContact,
        kApply,
        kConversion,
        kRender,
        kGpu,
        kAudio,
        kEditor,
        kDisplay,
        kSchedulerLateness,
        kPresentCpu,
        kPresentationInterval,
        kCount
    };
    using FrameTimes = std::array<std::uint64_t, static_cast<std::size_t>(Timing::kCount)>;

    ProcessResources ReadProcessResources();
    TimingSummary Summarize(std::string name, std::vector<std::uint64_t> values);
    std::string PerformanceJson(const PerformanceReport& report);

    // 標本の容量は開始時に確保する。毎フレームは追記だけとし、集計とOS照会は開始・終了・明示照会時に行う。
    class PerformanceCapture final
    {
      public:
        void Start(PerformanceConfig config, std::string source, std::string game);
        void Stop();
        void Frame(const FrameTimes& values, std::uint64_t gpu_frame, bool gpu_valid,
                   std::uint64_t submitted_frame = 0);
        void Host(Timing timing, std::uint64_t ns);
        PerformanceReport Report() const;
        bool active() const;
        bool collecting() const
        {
            return report_.state == "Collecting";
        }
        std::uint64_t reserved_bytes() const;

      private:
        PerformanceReport report_;
        std::array<std::vector<std::uint64_t>, static_cast<std::size_t>(Timing::kCount)> samples_;
        std::uint64_t last_gpu_frame_ = 0;
        std::uint64_t first_sample_frame_ = 0;
        std::uint64_t started_ns_ = 0;
    };
} // namespace gengine
