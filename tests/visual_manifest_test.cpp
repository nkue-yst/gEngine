#include "visual_manifest.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
    void Require(bool valid, const char* message)
    {
        if (valid == false) {
            throw std::runtime_error(message);
        }
    }

    gengine::VisualManifest Decode(const std::string& text)
    {
        return gengine::DecodeVisualManifest({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }
} // namespace

int main()
{
    try {
        const auto source = nlohmann::json{{"version", 1},
                                           {"atlas", "assets/sprites.png"},
                                           {"backdrop", "assets/background.png"},
                                           {"title_logo", "ui/logo.png"},
                                           {"sprites", {{0., 0., .5, .5}, {.5, .5, .5, .5}}},
                                           {"hostile", 1},
                                           {"friendly", 2}};
        const auto definition = Decode(source.dump());
        const auto roundtrip = Decode(gengine::SerializeVisualManifest(definition));
        Require(roundtrip.atlas == definition.atlas && roundtrip.sprites == definition.sprites &&
                    roundtrip.hostile == 1 && roundtrip.friendly == 2 && roundtrip.combat_brightness <= .35f,
                "visual manifest roundtrip changed the definition");

        const auto rejected = [&](const nlohmann::json& candidate) {
            try {
                Decode(candidate.dump());
            } catch (const std::exception&) {
                return;
            }
            throw std::runtime_error("invalid visual manifest accepted");
        };
        // 配布元を越える参照と、通常ファイル・packで解釈が違う名前を入口で拒否する。
        for (const auto* path :
             {"", "/a.png", "C:/a.png", "../a.png", "a/../b.png", "a//b.png", "a/./b.png", "a\\b.png", "a.jpg"}) {
            auto candidate = source;
            candidate["atlas"] = path;
            rejected(candidate);
        }
        for (const auto& uv : {nlohmann::json{0, 0, 0, .5}, nlohmann::json{.9, 0, .2, 1}, nlohmann::json{0, -1, 1, 1},
                               nlohmann::json{0, 0, 1}}) {
            auto candidate = source;
            candidate["sprites"] = {uv};
            rejected(candidate);
        }
        for (const auto* key : {"hostile", "friendly"}) {
            auto candidate = source;
            candidate[key] = 3;
            rejected(candidate);
            candidate[key] = -1;
            rejected(candidate);
        }
        auto candidate = source;
        candidate["detail_atlas"] = "assets/guardians.png";
        candidate["detail_sprites"] = {{0., 0., 1., 1.}};
        candidate["environment_atlas"] = "assets/places.png";
        candidate["environments"] = {{0., 0., 1., .5}, {0., .5, 1., .5}};
        const auto extended = Decode(gengine::SerializeVisualManifest(Decode(candidate.dump())));
        Require(extended.detail_sprites.size() == 1 && extended.environments.size() == 2,
                "atlas extensions did not roundtrip");
        candidate["detail_sprites"] = {{.8, 0, .3, 1}};
        rejected(candidate);
        candidate = source;
        candidate["environment_atlas"] = "assets/places.png";
        rejected(candidate);
        candidate["environments"] = {{0, 0, 1, 1}};
        candidate["environment_atlas"] = "../places.png";
        rejected(candidate);
        candidate = source;
        candidate["sprites"] = nlohmann::json::array();
        rejected(candidate);
        candidate = source;
        candidate["combat_brightness"] = .36;
        rejected(candidate);
        candidate = source;
        candidate["hostile_color"] = {1, 1, 2};
        rejected(candidate);
        candidate = source;
        candidate["title_logo"] = "";
        candidate["backdrop"] = "";
        candidate["hostile"] = 0;
        candidate["friendly"] = 0;
        Require(Decode(candidate.dump()).backdrop.empty(), "optional art was required");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
