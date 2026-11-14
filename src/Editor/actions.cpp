#include "actions.hpp"
#include <charconv>
#include <stdexcept>

namespace gengine::editor
{
    namespace
    {
        void WriteString(std::string& output, const std::string& value)
        {
            constexpr auto hex = "0123456789abcdef";
            output += '"';
            for (const unsigned char c : value) {
                if (c == '"' || c == '\\') {
                    output += '\\';
                    output += static_cast<char>(c);
                }
                else if (c < 0x20) {
                    output += "\\u00";
                    output += hex[c >> 4];
                    output += hex[c & 15];
                }
                else {
                    output += static_cast<char>(c);
                }
            }
            output += '"';
        }

        void WriteJson(std::string& output, const Value& value)
        {
            std::visit(
                [&](const auto& item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        output += "null";
                    }
                    else if constexpr (std::is_same_v<T, bool>) {
                        output += item ? "true" : "false";
                    }
                    else if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, std::int64_t>) {
                        output += std::to_string(item);
                    }
                    else if constexpr (std::is_same_v<T, double>) {
                        // ロケールや小数点以下の固定桁数に依存せず、再読込み可能な精度を保つ。
                        if (std::isfinite(item) == false) {
                            throw std::invalid_argument("action numbers must be finite");
                        }
                        char buffer[64];
                        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), item);
                        if (converted.ec != std::errc{}) {
                            throw std::invalid_argument("cannot serialize action number");
                        }
                        const auto text = std::string_view(buffer, converted.ptr);
                        output += text;
                        if (text.find_first_of(".eE") == std::string_view::npos) {
                            output += ".0";
                        }
                    }
                    else if constexpr (std::is_same_v<T, std::string>) {
                        WriteString(output, item);
                    }
                    else {
                        output += std::is_same_v<T, Value::Array> ? '[' : '{';
                        bool first = true;
                        for (const auto& child : item) {
                            if (first == false) {
                                output += ',';
                            }
                            first = false;
                            if constexpr (std::is_same_v<T, Value::Array>) {
                                WriteJson(output, child);
                            }
                            else {
                                WriteString(output, child.first);
                                output += ':';
                                WriteJson(output, child.second);
                            }
                        }
                        output += std::is_same_v<T, Value::Array> ? ']' : '}';
                    }
                },
                value.data);
        }

        bool Measure(const Value& value, std::size_t& remaining, unsigned depth)
        {
            if (depth > 16 || remaining < sizeof(Value)) {
                return false;
            }
            remaining -= sizeof(Value);
            return std::visit(
                [&](const auto& item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, std::string>) {
                        if (item.size() > remaining) {
                            return false;
                        }
                        remaining -= item.size();
                    }
                    else if constexpr (std::is_same_v<T, Value::Array> || std::is_same_v<T, Value::Object>) {
                        for (const auto& child : item) {
                            if constexpr (std::is_same_v<T, Value::Array>) {
                                if (Measure(child, remaining, depth + 1) == false) {
                                    return false;
                                }
                            }
                            else {
                                if (child.first.size() > remaining) {
                                    return false;
                                }
                                remaining -= child.first.size();
                                if (Measure(child.second, remaining, depth + 1) == false) {
                                    return false;
                                }
                            }
                        }
                    }
                    return true;
                },
                value.data);
        }

        bool ValidId(std::string_view id)
        {
            if (id.empty() || id.size() > 128) {
                return false;
            }
            for (const unsigned char c : id) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                    c == '-' || c == '.' || c == ':') {
                    continue;
                }
                return false;
            }
            return true;
        }

        constexpr std::size_t kRequestLimit = 64 * 1024;
        constexpr std::size_t kResultLimit = 256 * 1024;
    } // namespace

    std::string ToJson(const Value& value)
    {
        auto output = std::string{};
        WriteJson(output, value);
        return output;
    }

    bool WithinSize(const Value& value, std::size_t limit)
    {
        return Measure(value, limit, 0);
    }

    void Registry::Insert(ActionDefinition definition)
    {
        const auto id = definition.info.action_id;
        if (ValidId(id) == false || definitions_.emplace(id, std::move(definition)).second == false) {
            throw std::invalid_argument("invalid or duplicate action ID");
        }
    }

    const ActionDefinition* Registry::Find(std::string_view id) const
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() ? nullptr : &found->second;
    }

    std::vector<ActionInfo> Registry::List() const
    {
        auto result = std::vector<ActionInfo>{};
        for (const auto& [id, definition] : definitions_) {
            result.push_back(definition.info);
        }
        return result;
    }

    Dispatcher::Dispatcher(Registry registry, Now now)
        : registry_(std::move(registry)), now_(std::move(now)), owner_(std::this_thread::get_id())
    {
    }

    void Dispatcher::Prune(Clock::time_point now)
    {
        while (completed_.empty() == false &&
               (completed_.size() > 1024 || now - completed_.front().second >= std::chrono::minutes(15))) {
            entries_.erase(completed_.front().first);
            completed_.pop_front();
        }
    }

    Result<Operation> Dispatcher::Submit(const Request& request)
    {
        const auto* action = registry_.Find(request.action_id);
        if (action == nullptr) {
            return std::unexpected(ActionError{"NotFound", "action_id", "action is not registered"});
        }
        if (request.version != action->info.version) {
            return std::unexpected(ActionError{"InvalidArgument", "version", "unsupported action version"});
        }
        if ((request.operation_id.empty() && action->info.requires_operation_id) ||
            (request.operation_id.empty() == false && ValidId(request.operation_id) == false)) {
            return std::unexpected(ActionError{"InvalidArgument", "operation_id", "a valid operation ID is required"});
        }
        if (WithinSize(request.arguments, kRequestLimit) == false) {
            return std::unexpected(
                ActionError{"InvalidArgument", "arguments", "request exceeds size or nesting limit"});
        }
        auto prepared = action->prepare(request.arguments);
        if (prepared.has_value() == false) {
            return std::unexpected(prepared.error());
        }
        auto canonical = action->canonical(prepared->value);

        // 検証済み要求の照合と予約を同じロック内で行い、同時再送も一つにまとめる。
        const auto lock = std::lock_guard(mutex_);
        Prune(now_());
        if (const auto found = entries_.find(request.operation_id); found != entries_.end()) {
            const auto& previous = *found->second;
            if (previous.request.action_id != request.action_id || previous.request.version != request.version ||
                previous.request.arguments != canonical) {
                return std::unexpected(
                    ActionError{"Conflict", "operation_id", "operation ID belongs to a different request"});
            }
            return previous.operation;
        }
        if (waiting_.size() >= 128) {
            return std::unexpected(ActionError{"Busy", {}, "request queue is full; request was not accepted"});
        }
        auto entry = std::make_shared<Entry>();
        entry->request = {request.action_id, request.version,
                          request.operation_id.empty() ? "@" + std::to_string(++sequence_) : request.operation_id,
                          std::move(canonical)};
        entry->prepared = std::move(prepared->value);
        entry->operation = {entry->request.operation_id, "Queued", {}, {}};
        entries_.emplace(entry->request.operation_id, entry);
        waiting_.push_back(entry->request.operation_id);
        return entry->operation;
    }

    std::optional<ActionError> Dispatcher::Availability(std::string_view id, std::uint32_t version,
                                                        const Value& arguments) const
    {
        if (OnOwnerThread() == false) {
            return ActionError{"WrongThread", {}, "availability must run on the owner thread"};
        }
        const auto* action = registry_.Find(id);
        if (action == nullptr) {
            return ActionError{"NotFound", "action_id", "action is not registered"};
        }
        if (version != action->info.version) {
            return ActionError{"InvalidArgument", "version", "unsupported action version"};
        }
        if (WithinSize(arguments, kRequestLimit) == false) {
            return ActionError{"InvalidArgument", "arguments", "request exceeds size or nesting limit"};
        }
        auto prepared = action->prepare(arguments);
        if (prepared.has_value() == false) {
            return prepared.error();
        }
        return action->check(prepared->value);
    }

    Result<std::size_t> Dispatcher::Pump(std::size_t maximum)
    {
        if (OnOwnerThread() == false) {
            return std::unexpected(ActionError{"WrongThread", {}, "pump must run on the owner thread"});
        }
        if (pumping_) {
            return std::unexpected(ActionError{"Busy", {}, "dispatcher cannot be reentered"});
        }
        pumping_ = true;
        struct Reset
        {
            bool& flag;
            ~Reset()
            {
                flag = false;
            }
        } reset{pumping_};
        std::size_t processed = 0;
        while (processed < maximum) {
            auto entry = std::shared_ptr<Entry>{};
            {
                const auto lock = std::lock_guard(mutex_);
                if (waiting_.empty()) {
                    break;
                }
                entry = entries_.at(waiting_.front());
                waiting_.pop_front();
                entry->operation.state = "Running";
            }

            const auto* action = registry_.Find(entry->request.action_id);
            auto result = Result<Value>{};
            try {
                // 受付時に可能だった操作でも、実行直前の対象と状態を改めて検証する。
                const auto failure = action->check(entry->prepared);
                if (failure.has_value()) {
                    result = std::unexpected(*failure);
                }
                else {
                    result = action->execute(entry->prepared);
                }
                if (result.has_value() && WithinSize(*result, kResultLimit) == false) {
                    result = std::unexpected(
                        ActionError{"ResultTooLarge", {}, "operation ran, but its result exceeds retention limit"});
                }
            } catch (const std::exception&) {
                result =
                    std::unexpected(ActionError{"InternalError", {}, "handler failed; inspect target before retrying"});
            } catch (...) {
                result = std::unexpected(ActionError{"InternalError", {}, "handler raised an unknown exception"});
            }

            // 元の結果を保存してから保持範囲を整理する。実行中の要求は削除しない。
            {
                const auto lock = std::lock_guard(mutex_);
                entry->operation.state = result.has_value() ? "Succeeded" : "Failed";
                if (result.has_value()) {
                    entry->operation.result = std::move(*result);
                }
                else {
                    entry->operation.error = result.error();
                }
                entry->prepared.reset();
                const auto time = now_();
                completed_.emplace_back(entry->request.operation_id, time);
                Prune(time);
            }
            ++processed;
        }
        return processed;
    }

    OperationLookup Dispatcher::Lookup(std::string_view operation_id)
    {
        const auto lock = std::lock_guard(mutex_);
        Prune(now_());
        auto result = OperationLookup{};
        if (const auto found = entries_.find(operation_id); found != entries_.end()) {
            result.found = true;
            result.operation = found->second->operation;
        }
        result.completed_count = static_cast<std::uint32_t>(completed_.size());
        result.pending_count = static_cast<std::uint32_t>(entries_.size() - completed_.size());
        if (completed_.empty() == false) {
            result.oldest_completed_id = completed_.front().first;
            result.newest_completed_id = completed_.back().first;
        }
        return result;
    }
} // namespace gengine::editor
