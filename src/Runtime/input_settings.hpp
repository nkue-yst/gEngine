#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace gengine
{
    enum class InputAction : std::uint32_t
    {
        kLeft,
        kRight,
        kUp,
        kDown,
        kFocus,
        kPause,
        kDescend,
        kAscend
    };
    constexpr auto kPadLeftTrigger = std::uint32_t{1} << 16;
    constexpr auto kPadRightTrigger = std::uint32_t{1} << 17;
    struct InputSettings
    {
        std::array<std::uint32_t, 8> keys{'A', 'D', 'W', 'S', 0x20, 'P', 'Q', 'E'};
        // 集中・Pause・下降・上昇。移動とメニューの方向は固定する。
        std::array<std::uint32_t, 4> buttons{kPadRightTrigger, 0x10, 0x100, 0x200};
        bool operator==(const InputSettings&) const = default;
    };
    bool BindableKey(std::uint32_t key);
    bool BindableButton(std::uint32_t button);
    bool ValidInputSettings(const InputSettings& settings);
    // OSの言語やフォントに依存しない、同梱atlasで表示できる名称。
    std::wstring InputKeyName(std::uint32_t key);
    std::wstring InputButtonName(std::uint32_t button);
} // namespace gengine
