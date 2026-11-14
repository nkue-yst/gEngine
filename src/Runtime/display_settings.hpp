#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace gengine
{
    enum class DisplayMode : std::uint32_t
    {
        kWindowed,
        kBorderless,
        kFullscreen
    };
    struct Resolution
    {
        std::uint32_t width;
        std::uint32_t height;
        bool operator==(const Resolution&) const = default;
    };
    inline constexpr auto kGameResolutions =
        std::array{Resolution{1920, 1080}, Resolution{2560, 1440}, Resolution{3840, 2160}};
    struct DisplaySettings
    {
        DisplayMode mode = DisplayMode::kWindowed;
        Resolution resolution{1920, 1080};
        bool vsync = true;
        // 空ならモニター一覧の先頭。接続順ではなくWindowsの表示デバイス名を保存する。
        std::string monitor;
        bool operator==(const DisplaySettings&) const = default;
    };
    inline bool ValidDisplaySettings(const DisplaySettings& value)
    {
        if (value.mode > DisplayMode::kFullscreen || value.monitor.size() > 32) {
            return false;
        }
        for (const auto c : value.monitor) {
            if (c < 32 || c > 126) {
                return false;
            }
        }
        for (const auto resolution : kGameResolutions) {
            if (resolution == value.resolution) {
                return true;
            }
        }
        return false;
    }
} // namespace gengine
