#include "json.hpp"
#include "scene_document.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool value, const char* message)
    {
        if (value == false) {
            throw std::runtime_error(message);
        }
    }
    template <class F> void Reject(F function)
    {
        auto rejected = false;
        try {
            function();
        } catch (const std::exception&) {
            rejected = true;
        }
        Require(rejected, "invalid input was accepted");
    }
} // namespace

int main(int argc, char** argv)
{
    try {
        using namespace gengine;
        using namespace gengine::editor;
        Require(argc == 2, "expected project source directory");
        // 数値の境界をGUI側の型変換とJSON境界の両方で確認する。
        Require(Decode<float>(FromJson(ParseJson("-3.25"))).value() == -3.25f, "negative fractional value");
        Require(Decode<std::int32_t>(FromJson(ParseJson("30"))).value() == 30, "positive signed integer");
        Require(Decode<std::uint32_t>(FromJson(ParseJson("-1"))).has_value() == false, "negative unsigned");
        Require(Decode<std::int32_t>(FromJson(ParseJson("1.5"))).has_value() == false, "fractional integer");
        Require(Decode<float>(Value(1e100)).has_value() == false, "float overflow");
        Require(Decode<std::int32_t>(Value(std::uint64_t{2147483648})).has_value() == false, "integer overflow");
        for (const auto number : {-3.25, 0.0, 1.0, 1e-30, 1e30}) {
            Require(Decode<double>(FromJson(ParseJson(ToJson(Value(number))))).value() == number, "number roundtrip");
        }
        Reject([] {
            static_cast<void>(Value{std::numeric_limits<double>::infinity()});
        });
        Reject([] {
            static_cast<void>(Value{std::numeric_limits<double>::quiet_NaN()});
        });
        Require(AsJson(Schema<float>())["type"] == "number", "float schema");
        Require(AsJson(Schema<std::int32_t>())["minimum"] == std::numeric_limits<std::int32_t>::min(), "signed schema");

        // 両次元で正常な往復と、編集途中と試遊可能状態の違いを確認する。
        for (const auto three : {false, true}) {
            const auto scene = DefaultScene(three);
            const auto encoded = SerializeScene(scene);
            Require(ParseScene(encoded) == scene, "scene roundtrip");
            ValidateSceneForPlay(scene);
            auto partial = scene;
            partial.objects.erase(partial.objects.begin());
            Require(ParseScene(SerializeScene(partial)) == partial, "partial scene roundtrip");
            Reject([&] {
                ValidateSceneForPlay(partial);
            });
            const auto json = nlohmann::json::parse(encoded);
            for (auto which = 0; which < 12; ++which) {
                auto invalid = json;
                switch (which) {
                case 0:
                    invalid["format_version"] = 2;
                    break;
                case 1:
                    invalid["unknown"] = true;
                    break;
                case 2:
                    invalid["objects"][1]["id"] = "player";
                    break;
                case 3:
                    invalid["objects"][0]["position"] = {1};
                    break;
                case 4:
                    invalid["settings"]["player_hp"] = 1.5;
                    break;
                case 5:
                    invalid["settings"]["fire_interval_seconds"] = 0;
                    break;
                case 6:
                    invalid["settings"]["move_speed"] = 1e100;
                    break;
                case 7:
                    invalid["settings"]["bounds_min"] = invalid["settings"]["bounds_max"];
                    break;
                case 8:
                    invalid["objects"][0]["visual"] = "../outside.png";
                    break;
                case 9:
                    invalid["objects"][0]["position"][0] = 100;
                    break;
                case 10:
                    invalid["objects"][0]["visual"] = "C:/outside.png";
                    break;
                case 11:
                    invalid["objects"][0]["visual"] = "CON.png";
                    break;
                }
                Reject([&] {
                    ParseScene(invalid.dump());
                });
            }
            const auto sample = std::filesystem::path(argv[1]) / "samples" /
                                (scene.game == "3d" ? "shooter3d" : "shooter2d") / "project.json";
            Require(LoadProjectScene(sample, "scene.json") == scene, "bundled scene defaults differ");
            Reject([&] {
                LoadProjectScene(sample, "../scene.json");
            });
            Reject([&] {
                LoadProjectScene(sample, "unregistered.json");
            });
            auto referenced = scene;
            referenced.objects[0].visual = "assets/image.png";
            Require(ParseScene(SerializeScene(referenced)) == referenced, "reference roundtrip");
            Reject([&] {
                ValidateSceneReferences(referenced, argv[1]);
            });
            referenced.objects[0].visual = "samples/shooter2d/game.glang";
            ValidateSceneReferences(referenced, argv[1]);
            // 素材の形式検証は供給元を持つRuntimeで行い、ここでは文書構造だけを検証する。
            ValidateSceneForPlay(referenced);
        }
        Reject([] {
            ParseScene("{\"format_version\":1,\"format_version\":1}");
        });
        auto camera = DefaultScene(true);
        camera.objects[2].target = camera.objects[2].position;
        Reject([&] {
            ValidateScene(camera);
        });
        camera = DefaultScene(true);
        camera.objects[2].fov_degrees = 180;
        Reject([&] {
            ValidateScene(camera);
        });
        std::cout << "scene documents and numeric actions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
