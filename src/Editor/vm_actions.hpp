#pragma once
#include "actions.hpp"
#include "collection_types.hpp"
#include "metrics_types.hpp"
#include <gvm/gvm.h>

namespace gengine::editor
{
    struct EmptyRequest
    {
    };
    struct VmRequest
    {
        std::string vm_id;
    };
    struct AvailabilityRequest
    {
        std::string action_id;
        std::uint32_t version = 1;
        Value::Object arguments;
    };
    struct OperationRequest
    {
        std::string operation_id;
    };
    struct AvailabilityResult
    {
        bool available = false;
        std::optional<ActionError> error;
    };
    struct VmStateResult
    {
        std::string vm_id;
        std::string state;
        GvmStats stats{};
    };
    struct VmDiagnosticResult
    {
        bool present = false;
        std::string code;
        std::uint32_t detail = 0;
        std::optional<std::uint32_t> line;
        std::optional<std::uint32_t> column;
        std::string source;
        std::string function;
    };
    struct ResetResult
    {
        std::string vm_id;
        std::string state;
    };

    // VMの報告値とホストの計測を別項目にし、同じバイトを足し合わせない。
    struct VmMetricsResult
    {
        VmStateResult vm;
        GvmLimits limits{};
        GvmMemoryStats memory{};
        std::optional<CallMetrics> last_call;
        std::optional<InspectionMetrics> last_inspection;
    };

    template <> struct Record<EmptyRequest>
    {
        static auto Fields()
        {
            return std::tuple{};
        }
    };
    template <> struct Record<VmRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("vm_id", &VmRequest::vm_id)};
        }
    };
    template <> struct Record<OperationRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("operation_id", &OperationRequest::operation_id)};
        }
    };
    template <> struct Record<AvailabilityRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("action_id", &AvailabilityRequest::action_id),
                              MakeField("version", &AvailabilityRequest::version),
                              MakeField("arguments", &AvailabilityRequest::arguments)};
        }
    };
    template <> struct Record<AvailabilityResult>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("available", &AvailabilityResult::available),
                              MakeField("error", &AvailabilityResult::error)};
        }
    };
    template <> struct Record<GvmStats>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("owned_bytes", &GvmStats::owned_bytes),
                              MakeField("peak_owned_bytes", &GvmStats::peak_owned_bytes),
                              MakeField("stack_words", &GvmStats::stack_words),
                              MakeField("call_depth", &GvmStats::call_depth),
                              MakeField("fuel_remaining", &GvmStats::fuel_remaining)};
        }
    };
    template <> struct Record<VmStateResult>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("vm_id", &VmStateResult::vm_id), MakeField("state", &VmStateResult::state),
                              MakeField("stats", &VmStateResult::stats)};
        }
    };
    template <> struct Record<VmDiagnosticResult>
    {
        static auto Fields()
        {
            return std::tuple{
                MakeField("present", &VmDiagnosticResult::present),  MakeField("code", &VmDiagnosticResult::code),
                MakeField("detail", &VmDiagnosticResult::detail),    MakeField("line", &VmDiagnosticResult::line),
                MakeField("column", &VmDiagnosticResult::column),    MakeField("source", &VmDiagnosticResult::source),
                MakeField("function", &VmDiagnosticResult::function)};
        }
    };
    template <> struct Record<ResetResult>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("vm_id", &ResetResult::vm_id), MakeField("state", &ResetResult::state)};
        }
    };

    template <> struct Record<VmMetricsResult>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("vm", &VmMetricsResult::vm), MakeField("limits", &VmMetricsResult::limits),
                              MakeField("memory", &VmMetricsResult::memory),
                              MakeField("last_call", &VmMetricsResult::last_call),
                              MakeField("last_inspection", &VmMetricsResult::last_inspection)};
        }
    };

    // ホストがVMを所有する。破棄前にdetachし、操作側には安定したIDだけを公開する。
    class VmActions
    {
      public:
        explicit VmActions(Dispatcher::Now now = Dispatcher::Clock::now);
        Result<bool> Attach(std::string id, Gvm* vm, GvmLimits limits = gvm_default_limits());
        Result<bool> Detach(std::string_view id);
        // EditorのRegistryにも同じ操作を登録し、状態検査と処理を共有する。
        void RegisterVmActions(Registry& registry);
        Dispatcher& dispatcher()
        {
            return dispatcher_;
        }

      private:
        Registry Definitions();
        std::optional<ActionError> CheckVm(const VmRequest& request) const;
        Result<VmStateResult> State(const VmRequest& request) const;
        Result<VmDiagnosticResult> diagnostic(const VmRequest& request) const;
        Result<ResetResult> Reset(const VmRequest& request);
        Result<VmMetricsResult> Metrics(const VmRequest& request) const;
        std::optional<ActionError> CheckCall(const VmCallRequest& request) const;
        std::optional<ActionError> CheckPool(const PoolRequest& request) const;
        Result<ResetResult> Call(const VmCallRequest& request);
        Result<PoolSnapshot> Inspect(const PoolRequest& request);

        struct Registration
        {
            Gvm* vm = nullptr;
            GvmLimits limits{};
            std::optional<CallMetrics> last_call;
            std::optional<InspectionMetrics> last_inspection;
        };
        std::map<std::string, Registration, std::less<>> vms_;
        Dispatcher dispatcher_;
    };
} // namespace gengine::editor
