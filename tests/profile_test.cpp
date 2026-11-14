#include "game.hpp"
#include <fstream>
#include <iostream>

namespace
{
    void Require(bool value, const char* message)
    {
        if (value == false) {
            throw std::runtime_error(message);
        }
    }
} // namespace
int main(int argc, char** argv)
{
    try {
        Require(argc == 3, "profile test requires module and expectation");
        auto file = std::ifstream(argv[1], std::ios::binary);
        const auto bytes =
            std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto expected = std::string(argv[2]);
        try {
            auto game = gengine::Game(bytes);
            const auto initial = game.profile();
            auto actions = game.ProfileActions();
            if (expected != "valid" && expected != "reject" && expected.starts_with("reset") == false) {
                throw std::runtime_error("invalid profile contract was accepted");
            }
            Require(game.HasProfile() && initial.count == 1 && initial.values[0] == 20, "default profile differs");
            Require(game.ActOnProfile(99) == false && game.profile() == initial, "invalid action changed profile");
            const auto accepted = game.ActOnProfile(0);
            if (expected == "reject") {
                Require(accepted == false && game.profile() == initial, "rejected purchase was not rolled back");
                return 0;
            }
            Require(accepted && game.profile().values[0] == 15, "purchase not captured");
            game.Begin();
            Require(game.ActOnProfile(0) == false && game.RestoreProfile(initial) == false &&
                        game.ResetProfile() == false,
                    "playing profile was editable");
            game.Reset();
            Require(game.profile().values[0] == 15, "reset discarded profile");
            const auto purchased = game.profile();
            auto bad = initial;
            bad.schema = 99;
            Require(game.RestoreProfile(bad) == false && game.profile() == purchased, "future schema changed game");
            bad = initial;
            bad.count = 33;
            Require(game.RestoreProfile(bad) == false && game.profile() == purchased, "oversized restore changed game");
            bad = initial;
            bad.values[0] = 1000;
            Require(game.RestoreProfile(bad) == false && game.profile() == purchased, "invalid value changed game");
            auto reopened = gengine::Game(bytes);
            Require(reopened.RestoreProfile(purchased) && reopened.profile() == purchased, "new VM did not restore");
            if (expected.starts_with("reset")) {
                Require(game.HasProfileReset(), "optional reset export was not resolved");
                const auto reset = game.ResetProfile();
                if (expected == "reset") {
                    Require(reset && game.profile().values[0] == 0 && game.state().phase == gengine::Phase::kStart,
                            "reset did not apply progress defaults at title");
                    game.Begin();
                    Require(game.profile().values[0] == 0, "normal start restored old progress");
                }
                else {
                    Require(reset == false && game.profile() == purchased, "failed reset changed saved progress");
                    Require(game.state().phase ==
                                (expected == "reset-invalid" ? gengine::Phase::kFaulted : gengine::Phase::kStart),
                            "reset failure phase differs");
                }
            }
            else {
                Require(game.HasProfileReset() == false && game.ResetProfile() == false && game.profile() == purchased,
                        "legacy module acquired a reset action");
            }
            std::cout << "profile VM: reset, restart, actions, phase gates and rollback passed\n";
        } catch (const std::exception& error) {
            if (expected == "valid" || expected == "reject" || expected.starts_with("reset") ||
                std::string(error.what()).find(expected) == std::string::npos) {
                throw;
            }
            std::cout << "rejected: " << expected << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
