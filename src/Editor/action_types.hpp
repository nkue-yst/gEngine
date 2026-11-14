#pragma once

#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

namespace gengine::editor
{

    // 通信形式に依存しない値。現在の操作が必要とする型だけを扱う。
    struct Value
    {
        using Array = std::vector<Value>;
        using Object = std::map<std::string, Value, std::less<>>;
        std::variant<std::monostate, bool, std::uint64_t, std::int64_t, double, std::string, Array, Object> data;

        Value() = default;
        Value(bool value) : data(value)
        {
        }
        Value(std::uint32_t value) : data(static_cast<std::uint64_t>(value))
        {
        }
        Value(std::uint64_t value) : data(value)
        {
        }
        Value(std::int32_t value) : data(static_cast<std::int64_t>(value))
        {
        }
        Value(std::int64_t value) : data(value)
        {
        }
        Value(double value) : data(value)
        {
            if (std::isfinite(value) == false) {
                throw std::invalid_argument("action numbers must be finite");
            }
        }
        Value(const char* value) : data(std::string(value))
        {
        }
        Value(std::string value) : data(std::move(value))
        {
        }
        Value(Array value) : data(std::move(value))
        {
        }
        Value(Object value) : data(std::move(value))
        {
        }
        bool operator==(const Value&) const = default;
    };

    struct ActionError
    {
        std::string code;
        std::string field;
        std::string reason;
        std::optional<std::uint64_t> current_revision;
        std::vector<std::string> choices;
        bool operator==(const ActionError&) const = default;
    };

    template <class T> using Result = std::expected<T, ActionError>;
    template <class T> struct Record;

    template <class Owner, class Member> struct Field
    {
        const char* name;
        Member Owner::* member;
    };

    template <class Owner, class Member> constexpr auto MakeField(const char* name, Member Owner::* member)
    {
        return Field<Owner, Member>{name, member};
    }

    template <class T> struct Optional : std::false_type
    {
    };
    template <class T> struct Optional<std::optional<T>> : std::true_type
    {
        using Item = T;
    };
    template <class T> struct Array : std::false_type
    {
    };
    template <class T> struct Array<std::vector<T>> : std::true_type
    {
        using Item = T;
    };

    template <class T> Value Encode(const T& input);
    template <class T> Result<T> Decode(const Value& input, std::string path = {});
    template <class T> Value Schema();

    template <class T> Value Encode(const T& input)
    {
        if constexpr (std::is_same_v<T, Value>) {
            return input;
        }
        else if constexpr (std::is_same_v<T, Value::Object>) {
            return Value(input);
        }
        else if constexpr (std::is_floating_point_v<T>) {
            return Value(static_cast<double>(input));
        }
        else if constexpr (Optional<T>::value) {
            if (input.has_value()) {
                return Encode(*input);
            }
            return {};
        }
        else if constexpr (Array<T>::value) {
            auto values = Value::Array{};
            for (const auto& item : input) {
                values.push_back(Encode(item));
            }
            return values;
        }
        else if constexpr (requires { Record<T>::Fields(); }) {
            auto values = Value::Object{};
            std::apply(
                [&](const auto&... member) {
                    (values.emplace(member.name, Encode(input.*(member.member))), ...);
                },
                Record<T>::Fields());
            return values;
        }
        else {
            return Value(input);
        }
    }

    template <class T> Result<T> Decode(const Value& input, std::string path)
    {
        const auto bad = [&] {
            return std::unexpected(ActionError{"InvalidArgument", path, "value does not match the registered type"});
        };
        if constexpr (std::is_same_v<T, Value>) {
            return input;
        }
        else if constexpr (std::is_same_v<T, Value::Object>) {
            if (const auto* object = std::get_if<Value::Object>(&input.data); object != nullptr) {
                return *object;
            }
            return bad();
        }
        else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>) {
            if (const auto* value = std::get_if<T>(&input.data); value != nullptr) {
                return *value;
            }
            return bad();
        }
        else if constexpr (std::is_unsigned_v<T>) {
            if (const auto* value = std::get_if<std::uint64_t>(&input.data);
                value != nullptr && *value <= std::numeric_limits<T>::max()) {
                return static_cast<T>(*value);
            }
            return bad();
        }
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            if (const auto* value = std::get_if<std::int64_t>(&input.data); value != nullptr &&
                                                                            *value >= std::numeric_limits<T>::min() &&
                                                                            *value <= std::numeric_limits<T>::max()) {
                return static_cast<T>(*value);
            }
            if (const auto* value = std::get_if<std::uint64_t>(&input.data);
                value != nullptr && *value <= static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                return static_cast<T>(*value);
            }
            return bad();
        }
        else if constexpr (std::is_floating_point_v<T>) {
            // 数値だけを受け取り、狭い実数型へ変換した後も有限であることを確かめる。
            auto number = double{};
            if (const auto* value = std::get_if<double>(&input.data); value != nullptr) {
                number = *value;
            }
            else if (const auto* signed_value = std::get_if<std::int64_t>(&input.data); signed_value != nullptr) {
                number = static_cast<double>(*signed_value);
            }
            else if (const auto* unsigned_value = std::get_if<std::uint64_t>(&input.data); unsigned_value != nullptr) {
                number = static_cast<double>(*unsigned_value);
            }
            else {
                return bad();
            }
            if (std::isfinite(number) == false || number < -std::numeric_limits<T>::max() ||
                number > std::numeric_limits<T>::max()) {
                return bad();
            }
            return static_cast<T>(number);
        }
        else if constexpr (Optional<T>::value) {
            if (std::holds_alternative<std::monostate>(input.data)) {
                return T{};
            }
            auto value = Decode<typename Optional<T>::Item>(input, path);
            if (value.has_value() == false) {
                return std::unexpected(value.error());
            }
            return T{std::move(*value)};
        }
        else if constexpr (Array<T>::value) {
            const auto* values = std::get_if<Value::Array>(&input.data);
            if (values == nullptr) {
                return bad();
            }
            auto result = T{};
            for (std::size_t index = 0; index < values->size(); ++index) {
                auto item = Decode<typename Array<T>::Item>((*values)[index], path + "[" + std::to_string(index) + "]");
                if (item.has_value() == false) {
                    return std::unexpected(item.error());
                }
                result.push_back(std::move(*item));
            }
            return result;
        }
        else {
            const auto* values = std::get_if<Value::Object>(&input.data);
            if (values == nullptr) {
                return bad();
            }
            auto result = T{};
            auto failure = std::optional<ActionError>{};
            std::size_t known = 0;
            std::apply(
                [&](const auto&... member) {
                    const auto read = [&](const auto& item) {
                        using Member = std::remove_cvref_t<decltype(result.*(item.member))>;
                        const auto found = values->find(item.name);
                        const auto location = path.empty() ? std::string(item.name) : path + "." + item.name;
                        if (found == values->end()) {
                            if constexpr (Optional<Member>::value == false) {
                                failure = ActionError{"InvalidArgument", location, "required field is missing"};
                            }
                            return;
                        }
                        ++known;
                        auto value = Decode<Member>(found->second, location);
                        if (value.has_value() == false) {
                            failure = value.error();
                            return;
                        }
                        result.*(item.member) = std::move(*value);
                    };
                    (read(member), ...);
                },
                Record<T>::Fields());
            if (failure.has_value()) {
                return std::unexpected(*failure);
            }
            if (known != values->size()) {
                for (const auto& [name, value] : *values) {
                    bool recognized = false;
                    std::apply(
                        [&](const auto&... member) {
                            ((recognized = recognized || name == member.name), ...);
                        },
                        Record<T>::Fields());
                    if (recognized == false) {
                        return std::unexpected(
                            ActionError{"InvalidArgument", path.empty() ? name : path + "." + name, "unknown field"});
                    }
                }
            }
            return result;
        }
    }

    template <class T> Value Schema()
    {
        if constexpr (std::is_same_v<T, Value>) {
            return Value::Object{};
        }
        else if constexpr (std::is_same_v<T, Value::Object>) {
            return Value::Object{{"type", "object"}};
        }
        else if constexpr (std::is_same_v<T, bool>) {
            return Value::Object{{"type", "boolean"}};
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            return Value::Object{{"type", "string"}};
        }
        else if constexpr (std::is_unsigned_v<T>) {
            return Value::Object{{"type", "integer"},
                                 {"minimum", std::uint64_t{0}},
                                 {"maximum", static_cast<std::uint64_t>(std::numeric_limits<T>::max())}};
        }
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            return Value::Object{{"type", "integer"},
                                 {"minimum", static_cast<std::int64_t>(std::numeric_limits<T>::min())},
                                 {"maximum", static_cast<std::int64_t>(std::numeric_limits<T>::max())}};
        }
        else if constexpr (std::is_floating_point_v<T>) {
            return Value::Object{{"type", "number"},
                                 {"minimum", -static_cast<double>(std::numeric_limits<T>::max())},
                                 {"maximum", static_cast<double>(std::numeric_limits<T>::max())}};
        }
        else if constexpr (Optional<T>::value) {
            return Value::Object{
                {"anyOf", Value::Array{Schema<typename Optional<T>::Item>(), Value::Object{{"type", "null"}}}}};
        }
        else if constexpr (Array<T>::value) {
            return Value::Object{{"type", "array"}, {"items", Schema<typename Array<T>::Item>()}};
        }
        else {
            auto properties = Value::Object{};
            auto required = Value::Array{};
            std::apply(
                [&](const auto&... member) {
                    const auto add = [&](const auto& item) {
                        using Member = std::remove_cvref_t<decltype(std::declval<T>().*(item.member))>;
                        properties.emplace(item.name, Schema<Member>());
                        if constexpr (Optional<Member>::value == false) {
                            required.emplace_back(item.name);
                        }
                    };
                    (add(member), ...);
                },
                Record<T>::Fields());
            return Value::Object{{"type", "object"},
                                 {"properties", std::move(properties)},
                                 {"required", std::move(required)},
                                 {"additionalProperties", false}};
        }
    }

    // スキーマの出力だけに使う。入力のJSON解析はMCP adapterで扱う。
    std::string ToJson(const Value& value);
    bool WithinSize(const Value& value, std::size_t limit);

    template <> struct Record<ActionError>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("code", &ActionError::code), MakeField("field", &ActionError::field),
                              MakeField("reason", &ActionError::reason),
                              MakeField("current_revision", &ActionError::current_revision),
                              MakeField("choices", &ActionError::choices)};
        }
    };
} // namespace gengine::editor
