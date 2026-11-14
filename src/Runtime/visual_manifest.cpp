#include "visual_manifest.hpp"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace gengine
{
    namespace
    {
        void Require(bool valid, const char* message)
        {
            if (valid == false) {
                throw std::invalid_argument(message);
            }
        }

        void CheckPath(const std::string& path, bool optional)
        {
            if (optional && path.empty()) {
                return;
            }
            // packと通常ファイルで解釈が違う絶対パス・親参照・空の区間を定義段階で拒否する。
            Require(path.empty() == false && path.size() <= 512 && path.front() != '/' &&
                        path.find_first_of("\\:\0", 0, 3) == std::string::npos && path.ends_with(".png"),
                    "visual asset requires a relative PNG path");
            auto begin = std::size_t{0};
            while (begin < path.size()) {
                const auto end = path.find('/', begin);
                const auto part = path.substr(begin, end == std::string::npos ? end : end - begin);
                Require(part.empty() == false && part != "." && part != "..", "invalid visual asset path");
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }
        }
    } // namespace

    VisualManifest DecodeVisualManifest(std::span<const std::uint8_t> bytes)
    {
        Require(bytes.size() <= 16384, "visual manifest exceeds 16 KiB");
        const auto data = nlohmann::json::parse(bytes);
        Require(data.at("version") == 1, "unsupported visual manifest version");
        auto result = VisualManifest{};
        result.backdrop = data.value("backdrop", std::string{});
        result.title_logo = data.value("title_logo", std::string{});
        result.atlas = data.at("atlas").get<std::string>();
        CheckPath(result.backdrop, true);
        CheckPath(result.title_logo, true);
        CheckPath(result.atlas, false);

        const auto& sprites = data.at("sprites");
        Require(sprites.is_array() && sprites.empty() == false && sprites.size() <= 16,
                "visual atlas requires 1 to 16 sprites");
        for (const auto& sprite : sprites) {
            Require(sprite.is_array() && sprite.size() == 4, "visual UV requires four values");
            const auto rect = sprite.get<std::array<float, 4>>();
            for (const auto value : rect) {
                Require(std::isfinite(value) && value >= 0 && value <= 1, "visual UV is out of range");
            }
            Require(rect[2] > 0 && rect[3] > 0 && rect[0] + rect[2] <= 1 && rect[1] + rect[3] <= 1,
                    "visual UV extends outside the atlas");
            result.sprites.push_back(rect);
        }

        // 弾は0で従来表示を保ち、1以降をatlasの要素へ対応させる。
        for (const auto* key : {"hostile", "friendly"}) {
            Require(data.at(key).is_number_unsigned() && data.at(key).get<std::uint64_t>() <= sprites.size(),
                    "projectile sprite exceeds visual atlas");
        }
        result.hostile = data.at("hostile").get<std::uint32_t>();
        result.friendly = data.at("friendly").get<std::uint32_t>();
        if (data.contains("hostile_color")) {
            Require(data.at("hostile_color").size() == 3, "hostile color requires three values");
            result.hostile_color = data.at("hostile_color").get<std::array<float, 3>>();
        }
        if (data.contains("friendly_color")) {
            Require(data.at("friendly_color").size() == 3, "friendly color requires three values");
            result.friendly_color = data.at("friendly_color").get<std::array<float, 3>>();
        }
        for (const auto& color : {result.hostile_color, result.friendly_color}) {
            for (const auto value : color) {
                Require(std::isfinite(value) && value >= 0 && value <= 1, "visual color is out of range");
            }
        }
        result.combat_brightness = data.value("combat_brightness", .12f);
        // 大きな個体と場面の画像も通常の相対PNG参照で扱い、UV以外の切出し処理は持たない。
        const auto extension = [&](const char* path_key, const char* rect_key, std::string& path,
                                   std::vector<std::array<float, 4>>& rects) {
            path = data.value(path_key, std::string{});
            CheckPath(path, true);
            rects = data.value(rect_key, std::vector<std::array<float, 4>>{});
            Require(rects.size() <= 16 && (path.empty() == rects.empty()), "incomplete visual atlas extension");
            for (const auto& rect : rects) {
                for (const auto value : rect) {
                    Require(std::isfinite(value) && value >= 0 && value <= 1, "visual UV is out of range");
                }
                Require(rect[2] > 0 && rect[3] > 0 && rect[0] + rect[2] <= 1 && rect[1] + rect[3] <= 1,
                        "visual UV extends outside the atlas");
            }
        };
        extension("detail_atlas", "detail_sprites", result.detail_atlas, result.detail_sprites);
        extension("environment_atlas", "environments", result.environment_atlas, result.environments);
        Require(std::isfinite(result.combat_brightness) && result.combat_brightness >= 0 &&
                    result.combat_brightness <= .35f,
                "combat background brightness exceeds limit");
        return result;
    }

    std::string SerializeVisualManifest(const VisualManifest& manifest)
    {
        return nlohmann::json{{"version", 1},
                              {"backdrop", manifest.backdrop},
                              {"title_logo", manifest.title_logo},
                              {"atlas", manifest.atlas},
                              {"sprites", manifest.sprites},
                              {"detail_atlas", manifest.detail_atlas},
                              {"detail_sprites", manifest.detail_sprites},
                              {"environment_atlas", manifest.environment_atlas},
                              {"environments", manifest.environments},
                              {"hostile", manifest.hostile},
                              {"friendly", manifest.friendly},
                              {"hostile_color", manifest.hostile_color},
                              {"friendly_color", manifest.friendly_color},
                              {"combat_brightness", manifest.combat_brightness}}
                   .dump(2) +
               "\n";
    }
} // namespace gengine
