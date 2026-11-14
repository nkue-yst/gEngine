#pragma once
#include "action_types.hpp"

namespace gengine::editor
{
    // 引数なし・戻り値なしの公開関数を一回実行する。時間進行は後続版で扱う。
    struct VmCallRequest
    {
        std::string vm_id;
        std::string function;
        std::optional<std::uint64_t> fuel;
    };

    // ベクトルは成分ごとの列として指定する。scalarのcomponentは0。
    struct PoolColumnRequest
    {
        std::string field;
        std::uint32_t component = 0;
    };

    struct PoolRequest
    {
        std::string vm_id;
        std::string pool;
        std::vector<PoolColumnRequest> columns;
        std::uint32_t offset = 0;
        std::uint32_t limit = 16;
    };

    // 値は表示用文字列。f32の丸めや非有限値をJSONに委ねず、VMの型も併記する。
    struct PoolColumnSnapshot
    {
        std::string field;
        std::uint32_t component = 0;
        std::string type;
        std::vector<std::string> values;
    };

    // VMのポインターを含めない。表示や操作履歴が残っても借用は残らない。
    struct PoolSnapshot
    {
        std::string vm_id;
        std::string pool;
        std::uint32_t length = 0;
        std::uint32_t capacity = 0;
        std::uint32_t offset = 0;
        std::uint32_t count = 0;
        std::vector<PoolColumnSnapshot> columns;
    };

    template <> struct Record<VmCallRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("vm_id", &VmCallRequest::vm_id),
                              MakeField("function", &VmCallRequest::function), MakeField("fuel", &VmCallRequest::fuel)};
        }
    };
    template <> struct Record<PoolColumnRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("field", &PoolColumnRequest::field),
                              MakeField("component", &PoolColumnRequest::component)};
        }
    };
    template <> struct Record<PoolRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("vm_id", &PoolRequest::vm_id), MakeField("pool", &PoolRequest::pool),
                              MakeField("columns", &PoolRequest::columns), MakeField("offset", &PoolRequest::offset),
                              MakeField("limit", &PoolRequest::limit)};
        }
    };
    template <> struct Record<PoolColumnSnapshot>
    {
        static auto Fields()
        {
            return std::tuple{
                MakeField("field", &PoolColumnSnapshot::field), MakeField("component", &PoolColumnSnapshot::component),
                MakeField("type", &PoolColumnSnapshot::type), MakeField("values", &PoolColumnSnapshot::values)};
        }
    };
    template <> struct Record<PoolSnapshot>
    {
        static auto Fields()
        {
            return std::tuple{
                MakeField("vm_id", &PoolSnapshot::vm_id),    MakeField("pool", &PoolSnapshot::pool),
                MakeField("length", &PoolSnapshot::length),  MakeField("capacity", &PoolSnapshot::capacity),
                MakeField("offset", &PoolSnapshot::offset),  MakeField("count", &PoolSnapshot::count),
                MakeField("columns", &PoolSnapshot::columns)};
        }
    };
} // namespace gengine::editor
