#include "assets.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace gengine
{
    AudioManifest DecodeAudioManifest(std::span<const std::uint8_t> bytes)
    {
        const auto data = nlohmann::json::parse(bytes);
        const auto& events = data.at("events");
        const auto& tracks = data.at("tracks");
        if (data.at("version") != 1 || events.is_array() == false || events.size() < 4 || events.size() > 32 ||
            tracks.is_array() == false || tracks.size() > 8) {
            throw std::invalid_argument("invalid audio manifest");
        }
        auto result = AudioManifest{events.get<std::vector<std::string>>(), tracks.get<std::vector<std::string>>()};
        for (const auto& path : result.tracks) {
            if (path.empty()) {
                throw std::invalid_argument("music requires an asset path");
            }
        }
        for (const auto* field : {"title_track", "play_track"}) {
            if (data.at(field).is_number_unsigned() == false || data.at(field).get<std::uint64_t>() > tracks.size()) {
                throw std::invalid_argument("music track exceeds manifest");
            }
        }
        result.title_track = data.at("title_track").get<std::uint32_t>();
        result.play_track = data.at("play_track").get<std::uint32_t>();
        return result;
    }
    std::string SerializeAudioManifest(const AudioManifest& manifest)
    {
        return nlohmann::json{{"version", 1},
                              {"events", manifest.events},
                              {"tracks", manifest.tracks},
                              {"title_track", manifest.title_track},
                              {"play_track", manifest.play_track}}
                   .dump(2) +
               "\n";
    }
} // namespace gengine
