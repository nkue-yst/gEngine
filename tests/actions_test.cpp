#include "vm_actions.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace gengine::editor;

struct CounterRequest
{
    std::uint32_t amount = 0;
    std::optional<bool> flag;
};
struct CounterResult
{
    std::uint32_t value = 0;
};
template <> struct gengine::editor::Record<CounterRequest>
{
    static auto Fields()
    {
        return std::tuple{MakeField("amount", &CounterRequest::amount), MakeField("flag", &CounterRequest::flag)};
    }
};
template <> struct gengine::editor::Record<CounterResult>
{
    static auto Fields()
    {
        return std::tuple{MakeField("value", &CounterResult::value)};
    }
};

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition == false) {
            throw std::runtime_error(message);
        }
    }

    Registry CounterRegistry(std::uint32_t& counter, bool& ready)
    {
        auto registry = Registry{};
        const auto owner = std::this_thread::get_id();
        registry.Add<CounterRequest, CounterResult>(
            "test.add", "加算", "runtime-control", true,
            [&](const CounterRequest&) -> std::optional<ActionError> {
                if (ready == false) {
                    return ActionError{"InvalidState", {}, "counter is unavailable"};
                }
                return {};
            },
            [&, owner](const CounterRequest& request) -> Result<CounterResult> {
                Require(std::this_thread::get_id() == owner, "handler ran outside owner thread");
                counter += request.amount;
                return CounterResult{counter};
            });
        return registry;
    }

    void RegistrationAndDispatch()
    {
        std::uint32_t counter = 0;
        bool ready = true;
        auto dispatcher = Dispatcher(CounterRegistry(counter, ready));
        const auto request = CounterRequest{2, {}};
        const auto first = dispatcher.SubmitTyped("test.add", request, "one");
        Require(first.has_value() && first->state == "Queued" && counter == 0, "acceptance is not completion");
        Require(dispatcher.SubmitTyped("test.add", request, "one")->state == "Queued", "queued replay");
        Require(dispatcher.SubmitTyped("test.add", CounterRequest{3, {}}, "one").error().code == "Conflict",
                "conflicting replay");
        Require(dispatcher.SubmitTyped("test.add", request).error().code == "InvalidArgument", "mutation ID required");
        Require(dispatcher.Pump().value() == 1 && counter == 2, "exactly one application");
        const auto complete = dispatcher.Lookup("one");
        Require(complete.found && complete.operation->state == "Succeeded", "completed result");
        Require(Decode<CounterResult>(complete.operation->result)->value == 2, "typed output");
        counter = 10;
        Require(dispatcher.SubmitTyped("test.add", request, "one")->result == complete.operation->result &&
                    counter == 10,
                "completed replay");

        auto raw = Request{"test.add", 1, "invalid", Value::Object{{"amount", std::uint32_t{1}}, {"extra", true}}};
        Require(dispatcher.Submit(raw).error().code == "InvalidArgument", "unknown field");
        raw.arguments = Value::Object{{"amount", "wrong"}};
        Require(dispatcher.Submit(raw).error().field == "amount", "type validation location");
        raw.arguments = Value::Object{};
        Require(dispatcher.Submit(raw).error().field == "amount", "missing field");
        raw.arguments = Value::Object{{"amount", std::uint64_t{UINT32_MAX} + 1}};
        Require(dispatcher.Submit(raw).has_value() == false, "numeric range");
        raw.arguments = Value::Object{{"amount", std::uint32_t{1}}};
        raw.version = 2;
        Require(dispatcher.Submit(raw).has_value() == false, "definition version");
        raw.version = 1;
        Require(dispatcher.Submit(raw).has_value(), "validation failure did not reserve ID");
        Require(dispatcher.Availability("test.add", 1, raw.arguments).has_value() == false,
                "availability before state change");
        ready = false;
        Require(dispatcher.Availability("test.add", 1, raw.arguments)->code == "InvalidState",
                "availability shared validation");
        Require(dispatcher.Pump().value() == 1 && counter == 10, "execution-time validation");
        Require(dispatcher.Lookup("invalid").operation->error->code == "InvalidState", "state failure retained");

        const auto catalog = dispatcher.List();
        const auto& root = std::get<Value::Object>(catalog[0].input_schema.data);
        Require(std::get<bool>(root.at("additionalProperties").data) == false, "closed input schema");
        Require(std::get<Value::Array>(root.at("required").data) == Value::Array{"amount"}, "optional field schema");
        Require(ToJson(Value("\"\n\\\x01")) == "\"\\\"\\u000a\\\\\\u0001\"", "JSON escaping");
    }

    void ThreadsQueueAndRetention()
    {
        std::uint32_t counter = 0;
        bool ready = true;
        auto time = Dispatcher::Clock::time_point{};
        auto dispatcher = Dispatcher(CounterRegistry(counter, ready), [&] {
            return time;
        });
        auto accepted = std::array<bool, 8>{};
        auto workers = std::vector<std::thread>{};
        for (std::size_t index = 0; index < accepted.size(); ++index) {
            workers.emplace_back([&, index] {
                accepted[index] = dispatcher.SubmitTyped("test.add", CounterRequest{1, {}}, "concurrent").has_value();
                const auto wrong = dispatcher.Pump();
                accepted[index] = accepted[index] && wrong.has_value() == false && wrong.error().code == "WrongThread";
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        for (const bool value : accepted) {
            Require(value, "concurrent submit or wrong-thread rejection");
        }
        Require(dispatcher.Pump().value() == 1 && counter == 1, "concurrent deduplication");
        for (std::uint32_t index = 0; index < 128; ++index) {
            Require(
                dispatcher.SubmitTyped("test.add", CounterRequest{1, {}}, "queue-" + std::to_string(index)).has_value(),
                "queue capacity");
        }
        Require(dispatcher.SubmitTyped("test.add", CounterRequest{1, {}}, "retry").error().code == "Busy",
                "queue overflow");
        time += std::chrono::minutes(16);
        Require(dispatcher.Lookup("queue-0").operation->state == "Queued", "pending does not expire");
        Require(dispatcher.Pump().value() == 128, "drain queue");
        Require(dispatcher.SubmitTyped("test.add", CounterRequest{1, {}}, "retry").has_value(),
                "busy did not reserve ID");
        Require(dispatcher.Pump().value() == 1, "retry accepted");
        for (std::uint32_t index = 0; index < 1025; ++index) {
            Require(dispatcher.SubmitTyped("test.add", CounterRequest{0, {}}, "history-" + std::to_string(index))
                        .has_value(),
                    "history accept");
            Require(dispatcher.Pump().value() == 1, "history execute");
        }
        Require(dispatcher.Lookup("history-0").found == false, "count retention boundary");
        const auto retained = dispatcher.Lookup("history-1");
        Require(retained.found && retained.completed_count == 1024 && retained.oldest_completed_id == "history-1",
                "retention range");
        time += std::chrono::seconds(899);
        Require(dispatcher.Lookup("history-1").found, "before expiry");
        time += std::chrono::seconds(1);
        Require(dispatcher.Lookup("history-1").found == false, "expiry boundary");
    }

    void BoundedRequestsAndResults()
    {
        auto registry = Registry{};
        registry.Add<EmptyRequest, std::string>(
            "test.large", "大きな結果", "read", false,
            [](const EmptyRequest&) -> std::optional<ActionError> {
                return {};
            },
            [](const EmptyRequest&) -> Result<std::string> {
                return std::string(256 * 1024, 'x');
            });
        auto dispatcher = Dispatcher(std::move(registry));
        const auto large = Request{"test.large", 1, {}, Value::Object{{"unexpected", std::string(64 * 1024, 'x')}}};
        Require(dispatcher.Submit(large).has_value() == false, "request bound");
        const auto accepted = dispatcher.SubmitTyped("test.large", EmptyRequest{});
        Require(accepted.has_value() && dispatcher.Pump().value() == 1, "large result execution");
        Require(dispatcher.Lookup(accepted->operation_id).operation->error->code == "ResultTooLarge",
                "result retention bound");
    }

    void RunningReplay()
    {
        Dispatcher* dispatcher = nullptr;
        auto registry = Registry{};
        registry.Add<EmptyRequest, bool>(
            "test.run", "実行中の照会", "runtime-control", true,
            [](const EmptyRequest&) -> std::optional<ActionError> {
                return {};
            },
            [&](const EmptyRequest&) -> Result<bool> {
                Require(dispatcher->SubmitTyped("test.run", EmptyRequest{}, "running")->state == "Running",
                        "running replay");
                Require(dispatcher->Pump().error().code == "Busy", "pump reentry");
                return true;
            });
        auto instance = Dispatcher(std::move(registry));
        dispatcher = &instance;
        Require(instance.SubmitTyped("test.run", EmptyRequest{}, "running").has_value(), "running request");
        Require(instance.Pump().value() == 1 && instance.Lookup("running").operation->state == "Succeeded",
                "running completion");
    }

    std::uint32_t Add(void*, const GvmValue* arguments, std::size_t, GvmValue* result) noexcept
    {
        *result = {GVM_I32, {arguments[0].words[0] + arguments[1].words[0], 0, 0}};
        return 0;
    }
    std::uint32_t Fail(void*, const GvmValue* arguments, std::size_t, GvmValue*) noexcept
    {
        return arguments[0].words[0];
    }

    void TestVmActions(const char* filename)
    {
        auto input = std::ifstream(filename, std::ios::binary);
        Require(input.is_open(), "module input");
        const auto bytes =
            std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        const auto parameters = std::array<std::uint32_t, 2>{GVM_I32, GVM_I32};
        const std::uint32_t unsigned_type = GVM_U32;
        const auto imports = std::array{GvmImport{"host_add", 8, 1, parameters.data(), 2, GVM_I32, Add, nullptr},
                                        GvmImport{"host_fail", 9, 1, &unsigned_type, 1, GVM_VOID, Fail, nullptr}};
        Gvm* vm = nullptr;
        Require(gvm_create(1, bytes.data(), bytes.size(), gvm_default_limits(), imports.data(), imports.size(), &vm,
                           nullptr) == GVM_OK,
                "create action VM");
        struct Cleanup
        {
            Gvm* pointer;
            ~Cleanup()
            {
                gvm_destroy(pointer);
            }
        } cleanup{vm};
        auto service = VmActions{};
        Require(service.Attach("vm-1", vm).has_value(), "register VM");
        auto& dispatcher = service.dispatcher();
        const auto typed = dispatcher.SubmitTyped("vm.get_state", VmRequest{"vm-1"});
        const auto raw = dispatcher.Submit(Request{"vm.get_state", 1, {}, Value::Object{{"vm_id", "vm-1"}}});
        Require(dispatcher.Pump().value() == 2, "state queries");
        Require(dispatcher.Lookup(typed->operation_id).operation->result ==
                    dispatcher.Lookup(raw->operation_id).operation->result,
                "typed and structured parity");
        const auto state = Decode<VmStateResult>(dispatcher.Lookup(typed->operation_id).operation->result);
        Require(state->state == "Ready" && state->stats.owned_bytes > 0, "VM state snapshot");

        auto step = GvmExport{};
        auto count = GvmExport{};
        auto fault = GvmExport{};
        Require(gvm_resolve_export(vm, "step", 4, parameters.data(), 1, GVM_I32, &step, nullptr) == GVM_OK,
                "step export");
        Require(gvm_resolve_export(vm, "count", 5, nullptr, 0, GVM_I32, &count, nullptr) == GVM_OK, "count export");
        Require(gvm_resolve_export(vm, "fail", 4, nullptr, 0, GVM_VOID, &fault, nullptr) == GVM_OK, "fault export");
        auto output = GvmValue{};
        const auto amount = GvmValue{GVM_I32, {2, 0, 0}};
        Require(dispatcher.SubmitTyped("vm.reset", VmRequest{"vm-1"}, "reset-1").has_value(), "reset action");
        Require(dispatcher.Pump().value() == 1, "apply reset");
        Require(gvm_call(vm, step, &amount, 1, 100, &output, nullptr) == GVM_OK && output.words[0] == 12,
                "mutate after reset");
        Require(dispatcher.SubmitTyped("vm.reset", VmRequest{"vm-1"}, "reset-1")->state == "Succeeded", "reset replay");
        Require(dispatcher.Pump().value() == 0, "no replay execution");
        Require(gvm_call(vm, count, nullptr, 0, 100, &output, nullptr) == GVM_OK && output.words[0] == 12,
                "replay did not erase state");

        Require(gvm_call(vm, fault, nullptr, 0, 100, &output, nullptr) == GVM_ERROR, "fault VM");
        const auto diagnostic = dispatcher.SubmitTyped("vm.get_diagnostics", VmRequest{"vm-1"});
        Require(dispatcher.Pump().value() == 1, "diagnostic query");
        const auto result = Decode<VmDiagnosticResult>(dispatcher.Lookup(diagnostic->operation_id).operation->result);
        Require(result->present && result->code == "HostFailure" && result->detail == 42 && result->function == "fail",
                "copied diagnostic");
        Require(dispatcher.SubmitTyped("vm.reset", VmRequest{"vm-1"}, "reset-2").has_value(), "faulted reset");
        Require(dispatcher.Pump().value() == 1, "recover VM");
        Require(result->code == "HostFailure", "snapshot survives reset");
        const auto availability = dispatcher.SubmitTyped("action.get_availability",
                                                         AvailabilityRequest{"vm.reset", 1, {{"vm_id", "missing"}}});
        Require(dispatcher.Pump().value() == 1, "availability action");
        Require(
            Decode<AvailabilityResult>(dispatcher.Lookup(availability->operation_id).operation->result)->available ==
                false,
            "availability missing VM");
        const auto lookup = dispatcher.SubmitTyped("operation.get", OperationRequest{"reset-2"});
        Require(dispatcher.Pump().value() == 1 &&
                    Decode<OperationLookup>(dispatcher.Lookup(lookup->operation_id).operation->result)->found,
                "operation query action");

        Require(dispatcher.SubmitTyped("vm.reset", VmRequest{"vm-1"}, "removed").has_value(), "queue before removal");
        Require(service.Detach("vm-1").value(), "remove VM");
        Require(service.Attach("vm-1", vm).error().code == "Conflict", "retired VM ID cannot target a new instance");
        Require(dispatcher.Pump().value() == 1 && dispatcher.Lookup("removed").operation->error->code == "NotFound",
                "target revalidation");
    }
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::strcmp(argv[1], "--schemas") == 0) {
            auto service = VmActions{};
            std::cout << ToJson(Encode(service.dispatcher().List())) << '\n';
            return 0;
        }
        Require(argc == 2, "expected embedding module");
        RegistrationAndDispatch();
        ThreadsQueueAndRetention();
        RunningReplay();
        BoundedRequestsAndResults();
        TestVmActions(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
