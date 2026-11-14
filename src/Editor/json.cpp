#include "json.hpp"
#include <set>
#include <stdexcept>

namespace gengine::editor
{
    Json ParseJson(std::string_view text, std::size_t limit)
    {
        if (text.size() > limit) {
            throw std::invalid_argument("JSON exceeds the byte limit");
        }

        // オブジェクトごとにキーを記録し、後の値で黙って上書きされる重複を拒否する。
        auto keys = std::vector<std::set<std::string>>{};
        return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 32) {
                throw std::invalid_argument("JSON exceeds the nesting limit");
            }

            if (event == Json::parse_event_t::object_start) {
                keys.emplace_back();
            }
            else if (event == Json::parse_event_t::key) {
                if (keys.back().insert(value.get<std::string>()).second == false) {
                    throw std::invalid_argument("duplicate JSON key");
                }
            }
            else if (event == Json::parse_event_t::object_end) {
                keys.pop_back();
            }
            return true;
        });
    }

    Value FromJson(const Json& json)
    {
        // 共通操作が扱う単一の値は、その型のまま渡す。
        if (json.is_null()) {
            return {};
        }
        if (json.is_boolean()) {
            return Value(json.get<bool>());
        }
        if (json.is_number_unsigned()) {
            return Value(json.get<std::uint64_t>());
        }
        if (json.is_number_integer()) {
            return Value(json.get<std::int64_t>());
        }
        if (json.is_number_float()) {
            return Value(json.get<double>());
        }
        if (json.is_string()) {
            return Value(json.get<std::string>());
        }

        // 配列とオブジェクトは、中の値にも同じ型制約を適用する。
        if (json.is_array()) {
            auto result = Value::Array{};
            for (const auto& item : json) {
                result.push_back(FromJson(item));
            }
            return result;
        }

        if (json.is_object()) {
            auto result = Value::Object{};
            for (const auto& [key, item] : json.items()) {
                result.emplace(key, FromJson(item));
            }
            return result;
        }

        throw std::invalid_argument("unsupported action value");
    }

    Json AsJson(const Value& value)
    {
        // 共通操作側の直列化を使い、値の表現をGUIとMCPで揃える。
        return Json::parse(ToJson(value));
    }
} // namespace gengine::editor
