#include "platform.hpp"
#include "product.hpp"
#include "product_entry.hpp"
#include <iostream>
#include <vector>

using namespace gengine::editor;
namespace
{
    void Require(bool valid, const char* reason)
    {
        if (valid == false) {
            throw std::runtime_error(reason);
        }
    }
    template <class F> void Reject(F action)
    {
        try {
            action();
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("invalid product accepted");
    }
} // namespace
int wmain(int argc, wchar_t** argv)
{
    try {
        Require(argc == 3, "tools and output required");
        const auto tools = std::filesystem::path(argv[1]);
        const auto output = std::filesystem::path(argv[2]) / Wide(NewId());
        std::filesystem::create_directories(output);
        const auto valid = DecodeProductDefinition(
            R"({"version":1,"title":"日本語 Game","executable":"Game-1.exe","icon":"assets/game.ico"})");
        Require(valid.title == "日本語 Game" && valid.executable == "Game-1.exe", "product decoding differs");
        for (const auto* name : {"", "..\\escape.exe", "a/b.exe", "a..exe", "a.exe:stream", "CON.exe", "aux.exe",
                                 "LPT9.exe", "COM1.exe", "1Game.exe", "Game.EXE", "ゲーム.exe"}) {
            Reject([&] {
                ValidateProductExecutable(name);
            });
        }
        Reject([] {
            DecodeProductDefinition(R"({"version":2,"title":"Game","executable":"Game.exe","icon":"g.ico"})");
        });
        Reject([] {
            DecodeProductDefinition(R"({"version":1,"title":"Game\nName","executable":"Game.exe","icon":"g.ico"})");
        });

        // 既存の16px素材をICOへ包み、実PEのアイコンと入口をWindowsで読み返す。
        const auto png = ReadFile(tools / "content/assets/sprite.png", 1024 * 1024);
        auto icon = std::vector<std::uint8_t>{0, 0, 1, 0, 1, 0, 16, 16, 0, 0, 1, 0, 32, 0};
        for (const auto value : {static_cast<std::uint32_t>(png.size()), std::uint32_t{22}}) {
            for (auto byte = 0; byte < 4; ++byte) {
                icon.push_back(static_cast<std::uint8_t>(value >> (8 * byte)));
            }
        }
        icon.insert(icon.end(), png.begin(), png.end());
        const auto executable = output / "Product.exe";
        const auto original = ReadFile(tools / "gengine_runner.exe", 64 * 1024 * 1024);
        WriteNewFile(executable, original);
        EmbedProductResources(executable, icon);
        const auto modified = ReadFile(executable, 64 * 1024 * 1024);
        Require(modified != original && ReadFile(tools / "gengine_runner.exe", 64 * 1024 * 1024) == original,
                "source Runner was changed");
        const auto module = LoadLibraryExW(executable.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        Require(module != nullptr, "product resource load failed");
        const auto marker = FindResourceW(module, MAKEINTRESOURCEW(gengine::kProductMarkerId), RT_RCDATA);
        const auto data = marker != nullptr ? LoadResource(module, marker) : nullptr;
        const auto* text = data != nullptr ? static_cast<const char*>(LockResource(data)) : nullptr;
        const auto matched = text != nullptr && SizeofResource(module, marker) == gengine::kProductMarker.size() &&
                             std::string_view(text, gengine::kProductMarker.size()) == gengine::kProductMarker;
        const auto image = static_cast<HICON>(
            LoadImageW(module, MAKEINTRESOURCEW(gengine::kProductIconId), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        Require(matched && image != nullptr, "product marker or icon unreadable");
        DestroyIcon(image);
        FreeLibrary(module);

        // 不正な画像は書込み開始前に拒否し、既存の生成物を変更しない。
        for (const auto at : {0u, 4u, 6u, 14u, 18u, 22u}) {
            auto broken = icon;
            broken[at] ^= 0xff;
            Reject([&] {
                EmbedProductResources(executable, broken);
            });
            Require(ReadFile(executable, 64 * 1024 * 1024) == modified, "invalid icon changed executable");
        }
        Reject([&] {
            EmbedProductResources(executable, std::span(icon).first(21));
        });
        Reject([&] {
            EmbedProductResources(executable, std::vector<std::uint8_t>(1024 * 1024 + 1));
        });
        std::cout << "product names, UTF-8, PE marker/icon, corrupt ICO rejection and source preservation passed; "
                  << output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
