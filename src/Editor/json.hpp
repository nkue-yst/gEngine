#pragma once
#include "action_types.hpp"
#include <nlohmann/json.hpp>

namespace gengine::editor
{
    using Json = nlohmann::json;

    // 文書と通信の入口で容量、深さ、重複キーを検証する。
    Json ParseJson(std::string_view text, std::size_t limit = 1024 * 1024);

    // JSONと共通操作の値を相互変換する。整数の符号と実数を保持し、項目ごとの型検証はDecodeで行う。
    Value FromJson(const Json& json);
    Json AsJson(const Value& value);
} // namespace gengine::editor
