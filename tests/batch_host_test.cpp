#include "vm_actions.hpp"
#include <fstream>
#include <gvm/gvm.hpp>
#include <iostream>
#include <set>

using namespace gengine::editor;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition == false) {
            throw std::runtime_error(message);
        }
    }

    template <class Input> Operation Action(VmActions& actions, const char* name, const Input& request)
    {
        static auto sequence = std::uint64_t{};
        auto& dispatcher = actions.dispatcher();
        const auto accepted = dispatcher.SubmitTyped(name, request, std::string(name) + std::to_string(++sequence));
        Require(accepted.has_value(), "action not accepted");
        Require(dispatcher.Pump().has_value(), "action pump failed");
        return *dispatcher.Lookup(accepted->operation_id).operation;
    }

    template <class Output, class Input> Output Success(VmActions& actions, const char* name, const Input& request)
    {
        const auto result = Action(actions, name, request);
        if (result.error.has_value()) {
            throw std::runtime_error(std::string(name) + ": " + result.error->reason);
        }
        return Decode<Output>(result.result).value();
    }

    void CheckHost(gvm::Vm& vm)
    {
        // CLIの幅1/64と同じmainを呼び、全境界ケースの返り値も照合する。
        const auto result = vm.Call(vm.ResolveFunction<std::int32_t()>("main"));
        Require(result == 0, "reference suite failed in C++ host");
        std::cout << "main returned " << result << '\n';

        auto actions = VmActions{};
        Require(actions.Attach("batch", vm.native_handle()).has_value(), "attach failed");
        const auto request = VmRequest{"batch"};
        const auto metrics = [&] {
            return Success<VmMetricsResult>(actions, "vm.get_metrics", request);
        };
        const auto call = [&](const char* name, std::optional<std::uint64_t> fuel = {}) {
            return Success<ResetResult>(actions, "vm.call", VmCallRequest{"batch", name, fuel});
        };
        Success<ResetResult>(actions, "vm.reset", request);
        const auto initial = metrics();
        Require(initial.last_call.has_value() == false && initial.last_inspection.has_value() == false,
                "reset did not clear metrics");
        const auto& memory = initial.memory;
        Require(memory.module_bytes + memory.state_bytes + memory.pool_data_bytes + memory.pool_removal_bytes +
                        memory.stack_bytes + memory.kernel_bytes + memory.management_bytes ==
                    initial.vm.stats.owned_bytes,
                "memory breakdown differs from runtime owned bytes");
        Require(initial.vm.stats.peak_owned_bytes >= initial.vm.stats.owned_bytes && memory.pool_data_bytes > 0,
                "preparation peak and reserved pool bytes");
        call("initialize");
        call("update");
        const auto updated = metrics();
        const auto fuel = updated.last_call->fuel_consumed;
        Require(fuel > 1 && fuel + updated.last_call->fuel_remaining == kMaxCallFuel, "fuel accounting");

        // 64行ずつ最後の一行まで読む。浮動小数点・整数・真偽値とvec3のzを確認する。
        auto copied = PoolSnapshot{};
        for (const auto offset : {0u, 64u, 128u}) {
            copied = Success<PoolSnapshot>(actions, "vm.inspect_pool",
                                           PoolRequest{"batch",
                                                       "particles2d",
                                                       {{"position", 0},
                                                        {"position", 1},
                                                        {"velocity", 0},
                                                        {"velocity", 1},
                                                        {"life", 0},
                                                        {"serial", 0},
                                                        {"active", 0}},
                                                       offset,
                                                       64});
            const auto three = Success<PoolSnapshot>(actions, "vm.inspect_pool",
                                                     PoolRequest{"batch",
                                                                 "particles3d",
                                                                 {{"position", 0},
                                                                  {"position", 1},
                                                                  {"position", 2},
                                                                  {"velocity", 0},
                                                                  {"velocity", 1},
                                                                  {"velocity", 2},
                                                                  {"life", 0}},
                                                                 offset,
                                                                 64});
            Require(copied.length == 129 && copied.count == (offset == 128 ? 1u : 64u), "tail page");
            for (auto row = 0u; row < copied.count; ++row) {
                const auto serial = offset + row;
                const auto active = serial % 2 != 0;
                const auto& c = copied.columns;
                const auto& t = three.columns;
                Require(std::stof(c[0].values[row]) == static_cast<float>(serial) + (active ? 0.25f : 0.0f) &&
                            std::stof(c[1].values[row]) == (active ? -1.5f : -2.0f) && c[2].values[row] == "0.25" &&
                            c[3].values[row] == "0.5" &&
                            std::stoi(c[4].values[row]) == (active ? 100 / static_cast<int>(serial) : -1) &&
                            std::stoul(c[5].values[row]) == serial && c[6].values[row] == (active ? "true" : "false"),
                        "2D host snapshot differs from scalar oracle");
                Require(std::stof(t[0].values[row]) == static_cast<float>(serial) + 0.25f &&
                            t[1].values[row] == "1.5" && t[2].values[row] == "-1" && t[3].values[row] == "0.25" &&
                            t[4].values[row] == "-0.5" && t[5].values[row] == "2" && t[6].values[row] == "2",
                        "3D host snapshot differs from scalar oracle");
            }
        }
        const auto inspected = metrics();
        Require(inspected.last_inspection->source_value_bytes == 28 &&
                    inspected.last_inspection->vector_reserved_bytes >= 7 * sizeof(std::string) &&
                    inspected.last_inspection->string_capacity_bound_bytes >=
                        inspected.last_inspection->value_text_bytes,
                "host copy accounting");

        call("remove_expired");
        const auto survivors = Success<PoolSnapshot>(actions, "vm.inspect_pool",
                                                     PoolRequest{"batch", "particles2d", {{"serial", 0}}, 0, 64});
        auto ids = std::set<unsigned long>{};
        for (const auto& value : survivors.columns[0].values) {
            const auto id = std::stoul(value);
            Require(id < 129 && id % 2 != 0 && ids.insert(id).second, "invalid or duplicate survivor");
        }
        Require(survivors.length == 64 && ids.size() == 64 && copied.length == 129, "compaction or retained copy");

        // 同じ初期状態でちょうど必要な予算なら成功し、一単位不足ならFaultedになる。
        Success<ResetResult>(actions, "vm.reset", request);
        call("initialize");
        call("update", fuel);
        Require(metrics().last_call->fuel_remaining == 0, "exact fuel boundary");
        Success<ResetResult>(actions, "vm.reset", request);
        call("initialize");
        Require(Action(actions, "vm.call", VmCallRequest{"batch", "update", fuel - 1}).state == "Failed",
                "fuel shortage must fail");
        const auto faulted = metrics();
        const auto diagnostic = Success<VmDiagnosticResult>(actions, "vm.get_diagnostics", request);
        Require(faulted.vm.state == "Faulted" && faulted.last_call->succeeded == false &&
                    faulted.last_call->fuel_budget == fuel - 1 && diagnostic.code == "FuelExhausted" &&
                    diagnostic.line.has_value() && diagnostic.function == "update",
                "fault accounting and source");
        Require(Action(actions, "vm.call", VmCallRequest{"batch", "update", fuel}).error->code == "InvalidState",
                "faulted call must be rejected");

        Success<ResetResult>(actions, "vm.reset", request);
        for (const auto invalid : {std::uint64_t{0}, kMaxCallFuel + 1, UINT64_MAX}) {
            Require(Action(actions, "vm.call", VmCallRequest{"batch", "initialize", invalid}).error->code ==
                        "InvalidArgument",
                    "invalid fuel accepted");
        }
        Require(metrics().vm.state == "Ready" && metrics().last_call.has_value() == false, "invalid fuel changed VM");
        call("initialize");
        call("update");
        Require(metrics().vm.stats.owned_bytes == initial.vm.stats.owned_bytes &&
                    metrics().vm.stats.peak_owned_bytes == initial.vm.stats.peak_owned_bytes,
                "call/reset changed prepared VM ownership");

        // 時間に合否閾値を置かず、再現条件と分離した計測をログへ残す。
        std::cout << "updated=" << ToJson(Encode(updated)) << '\n';
        std::cout << "inspected=" << ToJson(Encode(inspected)) << '\n';
        std::cout << "faulted=" << ToJson(Encode(faulted)) << '\n';
        Require(actions.Detach("batch").has_value(), "detach failed");
        Require(Action(actions, "vm.get_metrics", request).error->code == "NotFound", "detached metrics");
    }
} // namespace

int main(int count, char** arguments)
{
    try {
        Require(count == 2, "usage: batch_host_test MODULE");
        auto input = std::ifstream(arguments[1], std::ios::binary);
        Require(input.is_open(), "cannot open fixture");
        const auto bytes =
            std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        auto vm = gvm::Vm(bytes);
        CheckHost(vm);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
