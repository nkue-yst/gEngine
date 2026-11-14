#include "vm_actions.hpp"
#include <cstring>

namespace gengine::editor
{
    namespace
    {
        ActionError VmError(GvmStatus status, const char* operation)
        {
            const char* code = status == GVM_BUSY            ? "Busy"
                               : status == GVM_WRONG_THREAD  ? "WrongThread"
                               : status == GVM_INVALID_STATE ? "InvalidState"
                                                             : "VmError";
            return {code, "vm_id", operation};
        }

        Result<std::string> CopyName(Gvm* vm, std::optional<std::uint32_t> function)
        {
            const auto copy = [&](char* buffer, std::size_t capacity, std::size_t* required) {
                return function.has_value() ? gvm_copy_function_name(vm, *function, buffer, capacity, required)
                                            : gvm_copy_source_name(vm, buffer, capacity, required);
            };
            std::size_t required = 0;
            const auto measured = copy(nullptr, 0, &required);
            if (measured != GVM_BUFFER_TOO_SMALL) {
                return std::unexpected(VmError(measured, "cannot measure diagnostic name"));
            }
            if (required > 64 * 1024) {
                return std::unexpected(ActionError{"ResultTooLarge", "source", "diagnostic name exceeds 64 KiB"});
            }
            auto value = std::string(required, '\0');
            const auto copied = copy(value.data(), value.size(), &required);
            if (copied != GVM_OK) {
                return std::unexpected(VmError(copied, "cannot copy diagnostic name"));
            }
            value.resize(required - 1);
            return value;
        }
    } // namespace

    VmActions::VmActions(Dispatcher::Now now) : dispatcher_(Definitions(), std::move(now))
    {
    }

    Registry VmActions::Definitions()
    {
        auto registry = Registry{};
        const auto always = [](const auto&) -> std::optional<ActionError> {
            return {};
        };
        registry.Add<EmptyRequest, std::vector<ActionInfo>>(
            "action.list", "登録済み操作と入出力スキーマを取得する", "read", false, always,
            [this](const EmptyRequest&) -> Result<std::vector<ActionInfo>> {
                return dispatcher_.List();
            });
        registry.Add<AvailabilityRequest, AvailabilityResult>(
            "action.get_availability", "現在の対象と状態で実行できるか確認する", "read", false, always,
            [this](const AvailabilityRequest& request) -> Result<AvailabilityResult> {
                auto error = dispatcher_.Availability(request.action_id, request.version, request.arguments);
                return AvailabilityResult{error.has_value() == false, std::move(error)};
            });
        registry.Add<OperationRequest, OperationLookup>(
            "operation.get", "保持中の要求と結果、保持範囲を取得する", "read", false, always,
            [this](const OperationRequest& request) -> Result<OperationLookup> {
                return dispatcher_.Lookup(request.operation_id);
            });
        RegisterVmActions(registry);
        return registry;
    }

    void VmActions::RegisterVmActions(Registry& registry)
    {
        const auto check = [this](const VmRequest& request) {
            return CheckVm(request);
        };
        registry.Add<VmRequest, VmStateResult>("vm.get_state", "VMの状態と管理領域の使用量を取得する", "read", false,
                                               check, [this](const VmRequest& request) {
                                                   return State(request);
                                               });
        registry.Add<VmRequest, VmDiagnosticResult>("vm.get_diagnostics", "最新の実行時診断とソース位置を取得する",
                                                    "read", false, check, [this](const VmRequest& request) {
                                                        return diagnostic(request);
                                                    });
        registry.Add<VmRequest, VmMetricsResult>("vm.get_metrics", "VMの予算・メモリ内訳とホストの変換費用を取得する",
                                                 "read", false, check, [this](const VmRequest& request) {
                                                     return Metrics(request);
                                                 });
        registry.Add<VmRequest, ResetResult>("vm.reset", "VMを宣言時の状態へ戻す", "runtime-control", true, check,
                                             [this](const VmRequest& request) {
                                                 return Reset(request);
                                             });
        registry.Add<VmCallRequest, ResetResult>(
            "vm.call", "引数なし・戻り値なしの公開関数を一回実行する", "runtime-control", true,
            [this](const VmCallRequest& request) {
                return CheckCall(request);
            },
            [this](const VmCallRequest& request) {
                return Call(request);
            });
        registry.Add<PoolRequest, PoolSnapshot>(
            "vm.inspect_pool", "集合の件数・容量と最大64個体・8列をコピーして取得する", "read", false,
            [this](const PoolRequest& request) {
                return CheckPool(request);
            },
            [this](const PoolRequest& request) {
                return Inspect(request);
            });
    }

    Result<bool> VmActions::Attach(std::string id, Gvm* vm, GvmLimits limits)
    {
        if (dispatcher_.OnOwnerThread() == false) {
            return std::unexpected(ActionError{"WrongThread", {}, "attach must run on the owner thread"});
        }
        if (id.empty() || id.size() > 128 ||
            id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXY"
                                 "Z0123456789_.:-") != std::string::npos ||
            vm == nullptr) {
            return std::unexpected(ActionError{"InvalidArgument", "vm_id", "invalid VM registration"});
        }
        std::uint32_t state = 0;
        const auto status = gvm_get_state(vm, &state);
        if (status != GVM_OK) {
            return std::unexpected(VmError(status, "VM must belong to the dispatcher owner"));
        }
        if (vms_.emplace(std::move(id), Registration{vm, limits, {}, {}}).second == false) {
            return std::unexpected(ActionError{"Conflict", "vm_id", "VM ID is already registered"});
        }
        return true;
    }

    Result<bool> VmActions::Detach(std::string_view id)
    {
        if (dispatcher_.OnOwnerThread() == false) {
            return std::unexpected(ActionError{"WrongThread", {}, "detach must run on the owner thread"});
        }
        const auto found = vms_.find(id);
        if (found == vms_.end() || found->second.vm == nullptr) {
            return false;
        }
        // 同じ起動内ではIDを再利用しない。待機中の要求が別のVMを操作することを防ぐ。
        found->second = {};
        return true;
    }

    std::optional<ActionError> VmActions::CheckVm(const VmRequest& request) const
    {
        const auto found = vms_.find(request.vm_id);
        if (found == vms_.end() || found->second.vm == nullptr) {
            return ActionError{"NotFound", "vm_id", "VM is not registered"};
        }
        std::uint32_t state = 0;
        const auto status = gvm_get_state(found->second.vm, &state);
        if (status != GVM_OK) {
            return VmError(status, "VM cannot be accessed in the current state");
        }
        if (state != GVM_READY && state != GVM_FAULTED) {
            return ActionError{"InvalidState", "vm_id", "VM is not ready or faulted"};
        }
        return {};
    }

    Result<VmStateResult> VmActions::State(const VmRequest& request) const
    {
        auto result = VmStateResult{};
        result.vm_id = request.vm_id;
        auto* vm = vms_.at(request.vm_id).vm;
        std::uint32_t state = 0;
        const auto status = gvm_get_state(vm, &state);
        if (status != GVM_OK) {
            return std::unexpected(VmError(status, "cannot read VM state"));
        }
        result.state = state == GVM_READY ? "Ready" : state == GVM_FAULTED ? "Faulted" : "Running";
        const auto stats = gvm_get_stats(vm, &result.stats);
        if (stats != GVM_OK) {
            return std::unexpected(VmError(stats, "cannot read VM statistics"));
        }
        return result;
    }

    Result<VmDiagnosticResult> VmActions::diagnostic(const VmRequest& request) const
    {
        auto result = VmDiagnosticResult{};
        auto* vm = vms_.at(request.vm_id).vm;
        auto diagnostic = GvmDiagnostic{};
        const auto status = gvm_get_diagnostic(vm, &diagnostic);
        if (status != GVM_OK) {
            return std::unexpected(VmError(status, "cannot read VM diagnostic"));
        }
        result.present = diagnostic.present != 0;
        if (result.present == false) {
            return result;
        }
        result.code = diagnostic.code;
        result.detail = diagnostic.detail;
        if (diagnostic.line != UINT32_MAX) {
            result.line = diagnostic.line;
        }
        if (diagnostic.column != UINT32_MAX) {
            result.column = diagnostic.column;
        }
        auto source = CopyName(vm, {});
        if (source.has_value() == false) {
            return std::unexpected(source.error());
        }
        result.source = std::move(*source);
        if (diagnostic.function != UINT32_MAX) {
            auto name = CopyName(vm, diagnostic.function);
            if (name.has_value() == false) {
                return std::unexpected(name.error());
            }
            result.function = std::move(*name);
        }
        return result;
    }

    Result<VmMetricsResult> VmActions::Metrics(const VmRequest& request) const
    {
        // Faultedでもruntimeの報告値を読み、失敗時の使用量と残り予算を調べられるようにする。
        auto state = State(request);
        if (state.has_value() == false) {
            return std::unexpected(state.error());
        }
        const auto& registration = vms_.at(request.vm_id);
        auto result = VmMetricsResult{
            std::move(*state), registration.limits, {}, registration.last_call, registration.last_inspection};
        const auto status = gvm_get_memory_stats(registration.vm, &result.memory);
        if (status != GVM_OK) {
            return std::unexpected(VmError(status, "cannot read VM memory breakdown"));
        }
        return result;
    }

    Result<ResetResult> VmActions::Reset(const VmRequest& request)
    {
        const auto status = gvm_reset(vms_.at(request.vm_id).vm, nullptr);
        if (status != GVM_OK) {
            return std::unexpected(VmError(status, "cannot reset VM"));
        }
        // 宣言時の状態へ戻ったため、以前の実行と表示の計測を現在値として残さない。
        auto& registration = vms_.at(request.vm_id);
        registration.last_call.reset();
        registration.last_inspection.reset();
        return ResetResult{request.vm_id, "Ready"};
    }
} // namespace gengine::editor
