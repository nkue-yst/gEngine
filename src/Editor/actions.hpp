#pragma once
#include "action_types.hpp"
#include <any>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace gengine::editor
{
    struct ActionInfo
    {
        std::string action_id;
        std::uint32_t version = 1;
        std::string description;
        std::string effect;
        bool requires_operation_id = false;
        Value input_schema;
        Value output_schema;
    };
    template <> struct Record<ActionInfo>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("action_id", &ActionInfo::action_id),
                              MakeField("version", &ActionInfo::version),
                              MakeField("description", &ActionInfo::description),
                              MakeField("effect", &ActionInfo::effect),
                              MakeField("requires_operation_id", &ActionInfo::requires_operation_id),
                              MakeField("input_schema", &ActionInfo::input_schema),
                              MakeField("output_schema", &ActionInfo::output_schema)};
        }
    };

    // 検証済みの型付き要求を保持する。型消去そのものを結果型の変換候補にしない。
    struct PreparedRequest
    {
        PreparedRequest() = default;
        std::any value;
    };

    struct ActionDefinition
    {
        ActionInfo info;
        std::function<Result<PreparedRequest>(const Value&)> prepare;
        std::function<Value(const std::any&)> canonical;
        std::function<std::optional<ActionError>(const std::any&)> check;
        std::function<Result<Value>(const std::any&)> execute;
    };

    // 登録は起動時だけ。Dispatcherへ渡した後の定義は変更しない。
    class Registry
    {
      public:
        template <class Request, class Output, class Check, class Execute>
        void Add(std::string id, std::string description, std::string effect, bool operation_required, Check check,
                 Execute execute)
        {
            auto definition = ActionDefinition{};
            definition.info = {
                std::move(id),   1, std::move(description), std::move(effect), operation_required, Schema<Request>(),
                Schema<Output>()};
            definition.prepare = [](const Value& arguments) -> Result<PreparedRequest> {
                auto request = Decode<Request>(arguments);
                if (request.has_value() == false) {
                    return std::unexpected(request.error());
                }
                auto prepared = PreparedRequest{};
                prepared.value = std::move(*request);
                return prepared;
            };
            definition.canonical = [](const std::any& request) {
                return Encode(std::any_cast<const Request&>(request));
            };
            definition.check = [check](const std::any& request) {
                return check(std::any_cast<const Request&>(request));
            };
            definition.execute = [execute](const std::any& request) -> Result<Value> {
                auto result = execute(std::any_cast<const Request&>(request));
                if (result.has_value() == false) {
                    return std::unexpected(result.error());
                }
                return Encode(*result);
            };
            Insert(std::move(definition));
        }
        std::vector<ActionInfo> List() const;

      private:
        friend class Dispatcher;
        const ActionDefinition* Find(std::string_view id) const;
        void Insert(ActionDefinition definition);
        std::map<std::string, ActionDefinition, std::less<>> definitions_;
    };

    struct Request
    {
        std::string action_id;
        std::uint32_t version = 1;
        std::string operation_id;
        Value arguments = Value::Object{};
    };

    struct Operation
    {
        std::string operation_id;
        std::string state;
        Value result;
        std::optional<ActionError> error;
    };
    template <> struct Record<Operation>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("operation_id", &Operation::operation_id),
                              MakeField("state", &Operation::state), MakeField("result", &Operation::result),
                              MakeField("error", &Operation::error)};
        }
    };

    struct OperationLookup
    {
        bool found = false;
        std::optional<Operation> operation;
        std::uint32_t max_completed = 1024;
        std::uint32_t retention_seconds = 900;
        std::uint32_t completed_count = 0;
        std::uint32_t pending_count = 0;
        std::optional<std::string> oldest_completed_id;
        std::optional<std::string> newest_completed_id;
    };
    template <> struct Record<OperationLookup>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("found", &OperationLookup::found),
                              MakeField("operation", &OperationLookup::operation),
                              MakeField("max_completed", &OperationLookup::max_completed),
                              MakeField("retention_seconds", &OperationLookup::retention_seconds),
                              MakeField("completed_count", &OperationLookup::completed_count),
                              MakeField("pending_count", &OperationLookup::pending_count),
                              MakeField("oldest_completed_id", &OperationLookup::oldest_completed_id),
                              MakeField("newest_completed_id", &OperationLookup::newest_completed_id)};
        }
    };

    class Dispatcher
    {
      public:
        using Clock = std::chrono::steady_clock;
        using Now = std::function<Clock::time_point()>;
        explicit Dispatcher(Registry registry, Now now = Clock::now);
        Dispatcher(const Dispatcher&) = delete;
        Dispatcher& operator=(const Dispatcher&) = delete;

        // 受付は任意のスレッドから可能。状態に依存する検証と実行はpumpだけが行う。
        Result<Operation> Submit(const Request& request);
        template <class T>
        Result<Operation> SubmitTyped(std::string id, const T& arguments, std::string operation_id = {})
        {
            return Submit(Request{std::move(id), 1, std::move(operation_id), Encode(arguments)});
        }
        Result<std::size_t> Pump(std::size_t maximum = 128);
        OperationLookup Lookup(std::string_view operation_id);
        std::optional<ActionError> Availability(std::string_view id, std::uint32_t version,
                                                const Value& arguments) const;
        std::vector<ActionInfo> List() const
        {
            return registry_.List();
        }
        bool OnOwnerThread() const
        {
            return owner_ == std::this_thread::get_id();
        }

      private:
        struct Entry
        {
            Request request;
            std::any prepared;
            Operation operation;
        };
        void Prune(Clock::time_point now);
        Registry registry_;
        Now now_;
        std::thread::id owner_;
        bool pumping_ = false;
        std::uint64_t sequence_ = 0;
        std::mutex mutex_;
        std::deque<std::string> waiting_;
        std::map<std::string, std::shared_ptr<Entry>, std::less<>> entries_;
        std::deque<std::pair<std::string, Clock::time_point>> completed_;
    };
} // namespace gengine::editor
