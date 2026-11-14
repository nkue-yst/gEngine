#include "performance.hpp"
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <numeric>
#include <windows.h>

#include <psapi.h>

namespace gengine
{
    namespace
    {
        std::uint64_t Now()
        {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  std::chrono::steady_clock::now().time_since_epoch())
                                                  .count());
        }
        constexpr auto kNames = std::array{"frame",
                                           "interval",
                                           "vm",
                                           "contact",
                                           "apply",
                                           "conversion",
                                           "render_present",
                                           "gpu",
                                           "audio",
                                           "editor_callback",
                                           "metrics_display",
                                           "scheduler_lateness",
                                           "present_cpu",
                                           "dxgi_present_interval"};
    } // namespace

    ProcessResources ReadProcessResources()
    {
        auto memory = PROCESS_MEMORY_COUNTERS_EX{};
        memory.cb = sizeof(memory);
        auto handles = DWORD{};
        auto created = FILETIME{};
        auto exited = FILETIME{};
        auto kernel = FILETIME{};
        auto user = FILETIME{};
        const auto process = GetCurrentProcess();
        if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) ==
                FALSE ||
            GetProcessHandleCount(process, &handles) == FALSE ||
            GetProcessTimes(process, &created, &exited, &kernel, &user) == FALSE) {
            throw std::runtime_error("cannot read process resources");
        }
        const auto time = [](FILETIME value) {
            return ((std::uint64_t{value.dwHighDateTime} << 32) | value.dwLowDateTime) * 100;
        };
        return {memory.PrivateUsage,
                memory.WorkingSetSize,
                memory.PeakWorkingSetSize,
                time(kernel) + time(user),
                handles,
                GetGuiResources(process, GR_GDIOBJECTS),
                GetGuiResources(process, GR_USEROBJECTS)};
    }

    TimingSummary Summarize(std::string name, std::vector<std::uint64_t> values)
    {
        auto result = TimingSummary{std::move(name)};
        if (values.empty()) {
            return result;
        }
        std::sort(values.begin(), values.end());
        result.samples = values.size();
        result.mean_ns = std::accumulate(values.begin(), values.end(), 0.0) / double(values.size());
        const auto percentile = [&](std::size_t percent) {
            return values[(values.size() * percent + 99) / 100 - 1];
        };
        result.p50_ns = percentile(50);
        result.p95_ns = percentile(95);
        result.p99_ns = percentile(99);
        result.max_ns = values.back();
        return result;
    }

    // 公開項目を固定し、単独実行とEditorで同じ意味・単位の結果を返す。
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TimingSummary, name, samples, mean_ns, p50_ns, p95_ns, p99_ns, max_ns)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PerformanceCounter, name, value)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProcessResources, private_bytes, working_set_bytes, peak_working_set_bytes,
                                       cpu_ns, handles, gdi_objects, user_objects)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PerformanceConfig, warmup, samples)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PerformanceReport, source, game, state, config, warmed, collected, elapsed_ns,
                                       timings, counters, process_begin, process_end)

    std::string PerformanceJson(const PerformanceReport& report)
    {
        return nlohmann::json(report).dump(2);
    }

    void PerformanceCapture::Start(PerformanceConfig config, std::string source, std::string game)
    {
        if (config.warmup > 600 || config.samples == 0 || config.samples > 3600) {
            throw std::invalid_argument("warmup must be 0..600; samples must be 1..3600");
        }
        // 失敗した開始で前回の結果を失わないよう、候補の確保後に入れ替える。
        auto next = decltype(samples_){};
        for (auto& samples : next) {
            samples.reserve(config.samples);
        }
        auto report = PerformanceReport{};
        report.source = std::move(source);
        report.game = std::move(game);
        report.config = config;
        report.state = config.warmup == 0 ? "Collecting" : "Warming";
        report.process_begin = ReadProcessResources();
        samples_ = std::move(next);
        report_ = std::move(report);
        last_gpu_frame_ = 0;
        first_sample_frame_ = 0;
        started_ns_ = Now();
    }

    bool PerformanceCapture::active() const
    {
        return report_.state == "Warming" || report_.state == "Collecting";
    }

    void PerformanceCapture::Stop()
    {
        if (active()) {
            report_.state = "Stopped";
            report_.elapsed_ns = Now() - started_ns_;
            report_.process_end = ReadProcessResources();
        }
    }

    void PerformanceCapture::Frame(const FrameTimes& values, std::uint64_t gpu_frame, bool gpu_valid,
                                   std::uint64_t submitted_frame)
    {
        if (active() == false) {
            return;
        }
        if (report_.warmed < report_.config.warmup) {
            ++report_.warmed;
            last_gpu_frame_ = gpu_valid ? gpu_frame : last_gpu_frame_;
            if (report_.warmed == report_.config.warmup) {
                report_.state = "Collecting";
            }
            return;
        }
        // 完了済みGPU時刻だけを一回ずつ記録する。CPUと同じ標本数とは限らない。
        if (first_sample_frame_ == 0) {
            first_sample_frame_ = submitted_frame;
        }
        for (auto i = std::size_t{0}; i <= static_cast<std::size_t>(Timing::kAudio); ++i) {
            if (i == static_cast<std::size_t>(Timing::kGpu) &&
                (gpu_valid == false || gpu_frame <= last_gpu_frame_ || gpu_frame < first_sample_frame_)) {
                continue;
            }
            samples_[i].push_back(values[i]);
        }
        if (gpu_valid) {
            last_gpu_frame_ = gpu_frame;
        }
        if (++report_.collected == report_.config.samples) {
            Stop();
            report_.state = "Complete";
        }
    }

    void PerformanceCapture::Host(Timing timing, std::uint64_t ns)
    {
        if (timing < Timing::kEditor || timing >= Timing::kCount) {
            throw std::invalid_argument("not a host timing");
        }
        auto& values = samples_[static_cast<std::size_t>(timing)];
        if (report_.state == "Collecting" && values.size() < report_.config.samples) {
            values.push_back(ns);
        }
    }

    PerformanceReport PerformanceCapture::Report() const
    {
        auto result = report_;
        if (active()) {
            result.elapsed_ns = Now() - started_ns_;
            result.process_end = ReadProcessResources();
        }
        for (auto i = std::size_t{0}; i < samples_.size(); ++i) {
            result.timings.push_back(Summarize(kNames[i], samples_[i]));
        }
        result.counters.push_back({"capture_reserved_bytes", reserved_bytes()});
        return result;
    }

    std::uint64_t PerformanceCapture::reserved_bytes() const
    {
        auto bytes = std::uint64_t{0};
        for (const auto& values : samples_) {
            bytes += values.capacity() * sizeof(std::uint64_t);
        }
        return bytes;
    }
} // namespace gengine
