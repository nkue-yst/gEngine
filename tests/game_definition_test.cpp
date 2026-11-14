#include "assets.hpp"
#include <BinPackLib/PackFiles.h>
#include <fstream>
#include <iostream>
#include <windows.h>

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
        throw std::runtime_error("invalid definition or path was accepted");
    }
    void Write(const std::filesystem::path& path, const std::string& text)
    {
        auto file = std::ofstream(path, std::ios::binary);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        Require(file.good(), "definition write failed");
    }
} // namespace
int main(int argc, char** argv)
{
    try {
        Require(argc == 3, "sample content and output required");
        const auto original = gengine::AssetSource(argv[1]);
        const auto root = std::filesystem::absolute(argv[2]) / std::to_string(GetTickCount64());
        for (const auto three_dimensional : {false, true}) {
            const auto expected = gengine::LoadGameInput(original, three_dimensional);
            const auto directory = root / (three_dimensional ? "3d" : "2d");
            std::filesystem::create_directories(directory / "bin");
            std::filesystem::create_directory(directory / "world");
            {
                auto file = std::ofstream(directory / "bin/play.glbc", std::ios::binary);
                file.write(reinterpret_cast<const char*>(expected.module.data()),
                           static_cast<std::streamsize>(expected.module.size()));
                Require(file.good(), "module copy failed");
            }
            Write(directory / "world/level.json", gengine::SerializeScene(expected.scene));
            auto definition = gengine::GameDefinition{"日本語 / Game", "bin/play.glbc", "world/level.json"};
            Write(directory / "game.json", gengine::SerializeGameDefinition(definition));
            const auto native = gengine::AssetSource(directory);
            Require(native.Exists("game.json") && native.Exists("missing.json") == false,
                    "optional definition detection differs");

            // 定義から次元を推定し、明示した次元だけは一致を要求する。
            const auto actual = gengine::LoadGameInput(native);
            Require(actual.title == definition.title && actual.module == expected.module &&
                        gengine::SerializeScene(actual.scene) == gengine::SerializeScene(expected.scene),
                    "named native input differs");
            Reject([&] {
                gengine::LoadGameInput(native, three_dimensional == false);
            });

            auto options = bin_pack_lib::PackOptions{};
            options.base_directory = directory.string();
            options.enable_compression = true;
            auto paths = std::vector<std::string>{};
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    paths.push_back(entry.path().string());
                }
            }
            const auto packed_path = root / (three_dimensional ? "3d.bpack" : "2d.bpack");
            Require(bin_pack_lib::PackFiles(paths, packed_path.string(), options).success, "definition pack failed");
            const auto packed = gengine::AssetSource(packed_path, true);
            const auto from_pack = gengine::LoadGameInput(packed);
            Require(from_pack.title == actual.title && from_pack.module == actual.module &&
                        gengine::SerializeScene(from_pack.scene) == gengine::SerializeScene(actual.scene),
                    "named pack input differs");
            Reject([&] {
                packed.Exists("../game.json");
            });
            Reject([&] {
                native.Exists("../game.json");
            });
            definition.module = "../outside.glbc";
            Write(directory / "game.json", gengine::SerializeGameDefinition(definition));
            Reject([&] {
                gengine::LoadGameInput(native);
            });
            Write(directory / "game.json", "{\"version\":2}");
            Reject([&] {
                gengine::LoadGameInput(native);
            });
            Write(directory / "game.json", "{}");
            Reject([&] {
                gengine::LoadGameInput(native);
            });
            for (const auto& title :
                 {std::string{}, std::string("line\nbreak"), std::string(257, 'a'), std::string("\xff")}) {
                definition.title = title;
                Reject([&] {
                    gengine::SerializeGameDefinition(definition);
                });
            }
        }
        std::cout << "named native/packed 2D/3D input, legacy fallback, UTF-8 and path rejection passed; " << root
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
