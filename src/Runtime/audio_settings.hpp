#pragma once
#include <cstdint>

namespace gengine
{
    struct AudioSettings
    {
        std::uint32_t master = 100;
        std::uint32_t music = 70;
        std::uint32_t effects = 100;
        bool muted = false;
        bool operator==(const AudioSettings&) const = default;
    };
    inline bool ValidAudioSettings(const AudioSettings& value)
    {
        return value.master <= 100 && value.music <= 100 && value.effects <= 100;
    }
} // namespace gengine
