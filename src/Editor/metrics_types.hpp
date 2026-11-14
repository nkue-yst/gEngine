#pragma once
#include "action_types.hpp"
#include <gvm/gvm.h>

namespace gengine::editor
{
    // 同期実行の上限は従来値を維持し、小さな予算で失敗と復帰を確認できるようにする。
    inline constexpr std::uint64_t kMaxCallFuel = 1'000'000;

    struct VmLoadRequest
    {
        std::string job_id;
        std::optional<std::uint64_t> max_owned_bytes;
    };

    // exportの解決や表示を除き、実際にgvm_callを実行した一回だけを計測する。
    struct CallMetrics
    {
        std::uint64_t fuel_budget = 0;
        std::uint64_t fuel_consumed = 0;
        std::uint64_t fuel_remaining = 0;
        std::uint64_t elapsed_ns = 0;
        bool succeeded = false;
        std::string function;
    };

    // 一件の表示用コピーに限定する。文字列容量には小文字列格納も含むため、ヒープ実測ではない。
    struct InspectionMetrics
    {
        std::uint64_t source_value_bytes = 0;
        std::uint64_t value_text_bytes = 0;
        std::uint64_t vector_reserved_bytes = 0;
        std::uint64_t string_capacity_bound_bytes = 0;
        std::uint64_t elapsed_ns = 0;
        std::string pool;
        std::uint32_t offset = 0;
        std::uint32_t count = 0;
        std::uint32_t columns = 0;
    };

    template <> struct Record<VmLoadRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("job_id", &VmLoadRequest::job_id),
                              MakeField("max_owned_bytes", &VmLoadRequest::max_owned_bytes)};
        }
    };
    template <> struct Record<GvmLimits>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("max_module_bytes", &GvmLimits::max_module_bytes),
                              MakeField("max_owned_bytes", &GvmLimits::max_owned_bytes),
                              MakeField("max_call_depth", &GvmLimits::max_call_depth)};
        }
    };
    template <> struct Record<GvmMemoryStats>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("module_bytes", &GvmMemoryStats::module_bytes),
                              MakeField("state_bytes", &GvmMemoryStats::state_bytes),
                              MakeField("pool_data_bytes", &GvmMemoryStats::pool_data_bytes),
                              MakeField("pool_removal_bytes", &GvmMemoryStats::pool_removal_bytes),
                              MakeField("stack_bytes", &GvmMemoryStats::stack_bytes),
                              MakeField("kernel_bytes", &GvmMemoryStats::kernel_bytes),
                              MakeField("management_bytes", &GvmMemoryStats::management_bytes)};
        }
    };
    template <> struct Record<CallMetrics>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("fuel_budget", &CallMetrics::fuel_budget),
                              MakeField("fuel_consumed", &CallMetrics::fuel_consumed),
                              MakeField("fuel_remaining", &CallMetrics::fuel_remaining),
                              MakeField("elapsed_ns", &CallMetrics::elapsed_ns),
                              MakeField("succeeded", &CallMetrics::succeeded),
                              MakeField("function", &CallMetrics::function)};
        }
    };
    template <> struct Record<InspectionMetrics>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("source_value_bytes", &InspectionMetrics::source_value_bytes),
                              MakeField("value_text_bytes", &InspectionMetrics::value_text_bytes),
                              MakeField("vector_reserved_bytes", &InspectionMetrics::vector_reserved_bytes),
                              MakeField("string_capacity_bound_bytes", &InspectionMetrics::string_capacity_bound_bytes),
                              MakeField("elapsed_ns", &InspectionMetrics::elapsed_ns),
                              MakeField("pool", &InspectionMetrics::pool),
                              MakeField("offset", &InspectionMetrics::offset),
                              MakeField("count", &InspectionMetrics::count),
                              MakeField("columns", &InspectionMetrics::columns)};
        }
    };
} // namespace gengine::editor
