#pragma once
#include "action_types.hpp"
#include "performance.hpp"

namespace gengine::editor
{
    struct PerformanceStart
    {
        std::string play_id;
        PerformanceConfig config;
    };

    // 計測の型付き定義をGUIとMCPで共用する。
    template <> struct Record<TimingSummary>
    {
        static auto Fields()
        {
            return std::tuple{
                MakeField("name", &TimingSummary::name),       MakeField("samples", &TimingSummary::samples),
                MakeField("mean_ns", &TimingSummary::mean_ns), MakeField("p50_ns", &TimingSummary::p50_ns),
                MakeField("p95_ns", &TimingSummary::p95_ns),   MakeField("p99_ns", &TimingSummary::p99_ns),
                MakeField("max_ns", &TimingSummary::max_ns)};
        }
    };
    template <> struct Record<PerformanceCounter>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("name", &PerformanceCounter::name),
                              MakeField("value", &PerformanceCounter::value)};
        }
    };
    template <> struct Record<ProcessResources>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("private_bytes", &ProcessResources::private_bytes),
                              MakeField("working_set_bytes", &ProcessResources::working_set_bytes),
                              MakeField("peak_working_set_bytes", &ProcessResources::peak_working_set_bytes),
                              MakeField("cpu_ns", &ProcessResources::cpu_ns),
                              MakeField("handles", &ProcessResources::handles),
                              MakeField("gdi_objects", &ProcessResources::gdi_objects),
                              MakeField("user_objects", &ProcessResources::user_objects)};
        }
    };
    template <> struct Record<PerformanceConfig>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("warmup", &PerformanceConfig::warmup),
                              MakeField("samples", &PerformanceConfig::samples)};
        }
    };
    template <> struct Record<PerformanceReport>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("source", &PerformanceReport::source),
                              MakeField("game", &PerformanceReport::game),
                              MakeField("state", &PerformanceReport::state),
                              MakeField("config", &PerformanceReport::config),
                              MakeField("warmed", &PerformanceReport::warmed),
                              MakeField("collected", &PerformanceReport::collected),
                              MakeField("elapsed_ns", &PerformanceReport::elapsed_ns),
                              MakeField("timings", &PerformanceReport::timings),
                              MakeField("counters", &PerformanceReport::counters),
                              MakeField("process_begin", &PerformanceReport::process_begin),
                              MakeField("process_end", &PerformanceReport::process_end)};
        }
    };
    template <> struct Record<PerformanceStart>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("play_id", &PerformanceStart::play_id),
                              MakeField("config", &PerformanceStart::config)};
        }
    };
} // namespace gengine::editor
