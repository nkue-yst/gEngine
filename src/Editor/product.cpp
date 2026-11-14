#include "product.hpp"
#include "assets.hpp"
#include "json.hpp"
#include "product_entry.hpp"
#include <algorithm>
#include <set>
#include <windows.h>

namespace gengine::editor
{
    void ValidateProductExecutable(std::string_view name)
    {
        const auto letter = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        };
        if (name.size() < 5 || name.size() > 68 || name.ends_with(".exe") == false || letter(name.front()) == false) {
            throw std::invalid_argument("product executable requires an ASCII name ending in .exe");
        }
        auto stem = std::string(name.substr(0, name.size() - 4));
        for (auto& c : stem) {
            if (letter(c) == false && (c < '0' || c > '9') && c != '-' && c != '_') {
                throw std::invalid_argument("product executable permits letters, digits, - and _ only");
            }
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem.back() >= '1' &&
             stem.back() <= '9')) {
            throw std::invalid_argument("product executable cannot use a reserved Windows device name");
        }
    }

    ProductDefinition DecodeProductDefinition(std::string_view text)
    {
        if (text.size() > 4096) {
            throw std::invalid_argument("product definition exceeds 4 KiB");
        }
        const auto data = Json::parse(text);
        if (data.at("version") != 1) {
            throw std::invalid_argument("unsupported product definition version");
        }
        auto result = ProductDefinition{data.at("title").get<std::string>(), data.at("executable").get<std::string>(),
                                        data.at("icon").get<std::string>()};
        // ウィンドウ名は通常ファイル・packと同じ規則で検査する。
        SerializeGameDefinition({result.title, "game.glbc", "scenes/game.json"});
        ValidateProductExecutable(result.executable);
        if (result.icon.empty() || result.icon.size() > 240 || result.icon.ends_with(".ico") == false) {
            throw std::invalid_argument("product icon requires an .ico asset path");
        }
        return result;
    }

    void EmbedProductResources(const std::filesystem::path& executable, std::span<const std::uint8_t> icon)
    {
        const auto require = [](bool valid) {
            if (valid == false) {
                throw std::invalid_argument("product ICO requires 1..8 distinct square PNG images, 16..256 px");
            }
        };
        require(icon.size() >= 22 && icon.size() <= 1024 * 1024);
        const auto u16 = [&](std::size_t at) {
            return std::uint16_t(icon[at]) | (std::uint16_t(icon[at + 1]) << 8);
        };
        const auto u32 = [&](std::size_t at) {
            return std::uint32_t(icon[at]) | (std::uint32_t(icon[at + 1]) << 8) | (std::uint32_t(icon[at + 2]) << 16) |
                   (std::uint32_t(icon[at + 3]) << 24);
        };
        const auto count = u16(4);
        require(u16(0) == 0 && u16(2) == 1 && count >= 1 && count <= 8 && icon.size() >= 6u + count * 16u);
        auto group = std::vector<std::uint8_t>(icon.begin(), icon.begin() + 6);
        auto images = std::vector<std::span<const std::uint8_t>>{};
        auto sizes = std::set<std::uint32_t>{};
        auto previous_end = std::size_t{6} + count * 16;
        // ICOのディレクトリをそのままポインターへ変換せず、範囲と画像寸法を先に検査する。
        for (auto index = 0; index < count; ++index) {
            const auto at = std::size_t{6} + index * 16;
            const auto width = icon[at] == 0 ? 256u : icon[at];
            const auto height = icon[at + 1] == 0 ? 256u : icon[at + 1];
            const auto length = u32(at + 8);
            const auto offset = u32(at + 12);
            require(width >= 16 && width == height && sizes.insert(width).second && icon[at + 2] == 0 &&
                    icon[at + 3] == 0 && u16(at + 4) == 1 && u16(at + 6) == 32 && offset >= previous_end &&
                    offset <= icon.size() && length <= icon.size() - offset && length > 0);
            const auto bytes = icon.subspan(offset, length);
            const auto decoded = DecodePng(bytes);
            require(decoded.width == width && decoded.height == height);
            previous_end = std::size_t{offset} + length;
            images.push_back(bytes);
            group.insert(group.end(), icon.begin() + at, icon.begin() + at + 12);
            group.push_back(static_cast<std::uint8_t>(index + 1));
            group.push_back(0);
        }

        // Win32の更新トランザクションにまとめ、途中失敗では既存リソースを保つ。
        const auto update = BeginUpdateResourceW(executable.c_str(), FALSE);
        if (update == nullptr) {
            throw std::runtime_error("cannot open product executable resources");
        }
        const auto put = [&](LPCWSTR type, WORD id, std::span<const std::uint8_t> bytes) {
            return UpdateResourceW(update, type, MAKEINTRESOURCEW(id), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                                   const_cast<std::uint8_t*>(bytes.data()), static_cast<DWORD>(bytes.size())) != FALSE;
        };
        auto success = true;
        for (auto index = std::size_t{}; index < images.size() && success; ++index) {
            success = put(RT_ICON, static_cast<WORD>(index + 1), images[index]);
        }
        success = success && put(RT_GROUP_ICON, kProductIconId, group) &&
                  put(RT_RCDATA, kProductMarkerId,
                      {reinterpret_cast<const std::uint8_t*>(kProductMarker.data()), kProductMarker.size()});
        const auto finished = EndUpdateResourceW(update, success ? FALSE : TRUE);
        if (success == false || finished == FALSE) {
            throw std::runtime_error("cannot commit product executable resources");
        }
    }
} // namespace gengine::editor
