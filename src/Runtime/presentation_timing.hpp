#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <rengine/diagnostics.hpp>

namespace gengine
{
    // 連続した提示を同じvblankとの対応で観測できた場合だけ、表示側の間隔を求める。
    // 重複・欠測・モード変更・32bitカウンターの折返しを平均値で埋めない。
    inline std::optional<std::uint64_t> PresentationInterval(const rengine::PresentationTiming& before,
                                                             const rengine::PresentationTiming& after)
    {
        if (before.available == 0 || after.available == 0 || before.disjoint != 0 || after.disjoint != 0 ||
            after.present_count != before.present_count + 1 || after.sync_qpc <= before.sync_qpc ||
            after.present_refresh_count <= before.present_refresh_count ||
            after.sync_refresh_count <= before.sync_refresh_count || before.qpc_frequency == 0 ||
            before.qpc_frequency != after.qpc_frequency) {
            return {};
        }
        const auto refresh_delta = after.present_refresh_count - before.present_refresh_count;
        if (refresh_delta != after.sync_refresh_count - before.sync_refresh_count) {
            return {};
        }
        const auto interval = double(after.sync_qpc - before.sync_qpc) * 1e9 / double(after.qpc_frequency);
        if (interval >= double(std::numeric_limits<std::uint64_t>::max())) {
            return {};
        }
        return static_cast<std::uint64_t>(interval);
    }
} // namespace gengine
