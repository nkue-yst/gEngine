#include "game.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool valid, const char* message)
    {
        if (valid == false) {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main(int argc, char** argv)
{
    try {
        Require(argc == 4, "module, expected behavior and dimension required");
        auto input = std::ifstream(argv[1], std::ios::binary);
        Require(input.good(), "missing actor fixture");
        const auto bytes = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
        const auto expected = std::string(argv[2]);
        const auto mode = std::string_view(argv[3]) == "3d" ? gengine::GameMode::k3d : gengine::GameMode::k2d;
        try {
            auto game = gengine::Game(bytes, 1000000, mode);
            Require(game.HasActors() && game.actors().size() == 2, "actor snapshot missing");
            game.Begin();
            game.Step({});
            if (expected != "valid") {
                Require(game.state().phase == gengine::Phase::kFaulted, "invalid actor contract was accepted");
                Require(game.state().diagnostic.find(expected) != std::string::npos, "wrong actor fault");
                return 0;
            }
            // 一発が二個体を貫いても先頭の一体だけへ通知し、自機接触は別のmaskで扱う。
            Require(game.state().phase == gengine::Phase::kPlaying && game.state().enemy_hp == 90,
                    "swept shot did not hit exactly the first eligible actor");
            Require(game.state().hp == 6, "player contact notification missing or duplicated");
            Require(game.state().status_count == 1 && game.state().status[0].value == 90 &&
                        game.state().status[0].maximum == 100,
                    "HUD did not reflect applied contacts");
            Require(game.actors().size() == 1 && game.Dots().empty(), "finish_step did not remove spent rows");
            game.Reset();
            Require(game.state().phase == gengine::Phase::kStart && game.actors().size() == 2 && game.state().hp == 5,
                    "reset retained actor state");
            // 表示契約はVMから読み出した値を保ち、宣言したプリセットだけを受け入れる。
            Require(game.actors()[0].opacity == 1.0f || game.actors()[0].opacity == 0.4f,
                    "actor opacity was not copied");
            if (game.state().presentation.scene != 0) {
                Require(game.state().presentation.scene == 3 && game.state().presentation.cue == 10 &&
                            game.state().presentation.cue_seconds == 2.5f,
                        "presentation snapshot differs");
            }
            if (game.HasWindowViews()) {
                const auto& views = game.state().windows;
                Require(views.count == 3 && views.scale == 0.75f && views.size.x == 12.0f && views.size.y == 6.75f &&
                            views.centers[2].x == -16.0f,
                        "window snapshot differs");
                game.Reset(9);
                auto rejected = false;
                try {
                    game.Reset(10);
                } catch (const std::exception&) {
                    rejected = true;
                }
                Require(rejected, "undeclared preview preset accepted");
                game.Reset();
            }
            game.Begin();
            game.Step({});
            Require(game.state().enemy_hp == 90 && game.state().hp == 6, "actor replay differs");
        } catch (const std::exception& error) {
            if (expected == "valid" || std::string(error.what()).find(expected) == std::string::npos) {
                throw;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
