#pragma once
#include <cstdint>
#include <string_view>

namespace gengine
{
    // Editorが生成した製品用EXEだけが、隣のgame.bpackを既定の入口にする。
    inline constexpr auto kProductMarkerId = std::uint16_t{101};
    inline constexpr auto kProductIconId = std::uint16_t{1};
    inline constexpr auto kProductMarker = std::string_view{"gEngine product 1"};
} // namespace gengine
