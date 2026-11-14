#include "assets.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <windows.h>

namespace gengine
{
    namespace
    {
        void Validate(const GameDefinition& definition)
        {
            // ウィンドウ名は短い一行のUTF-8。パスの実解決はAssetSourceへ委譲する。
            const auto& title = definition.title;
            if (title.empty() || title.size() > 256 ||
                std::any_of(title.begin(), title.end(),
                            [](unsigned char c) {
                                return c < 32 || c == 127;
                            }) ||
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(), static_cast<int>(title.size()),
                                    nullptr, 0) == 0) {
                throw std::invalid_argument("game title requires 1..256 bytes of single-line UTF-8");
            }
            if (definition.module.empty() || definition.module.size() > 240 ||
                definition.module.ends_with(".glbc") == false || definition.scene.empty() ||
                definition.scene.size() > 240 || definition.scene.ends_with(".json") == false) {
                throw std::invalid_argument("game entry requires .glbc module and .json scene paths");
            }
        }
    } // namespace
    GameDefinition DecodeGameDefinition(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() > 4096) {
            throw std::invalid_argument("game definition exceeds 4 KiB");
        }
        const auto data = nlohmann::json::parse(bytes);
        if (data.at("version") != 1) {
            throw std::invalid_argument("unsupported game definition version");
        }
        auto result = GameDefinition{data.at("title").get<std::string>(), data.at("module").get<std::string>(),
                                     data.at("scene").get<std::string>()};
        Validate(result);
        return result;
    }
    std::string SerializeGameDefinition(const GameDefinition& definition)
    {
        Validate(definition);
        return nlohmann::json{{"version", 1},
                              {"title", definition.title},
                              {"module", definition.module},
                              {"scene", definition.scene}}
                   .dump(2) +
               "\n";
    }
} // namespace gengine
