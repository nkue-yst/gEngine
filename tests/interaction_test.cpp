#include "game.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

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
        Require(argc == 3, "module and expected behavior required");
        auto stream = std::ifstream(argv[1], std::ios::binary);
        Require(stream.good(), "missing fixture");
        const auto bytes = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), {});
        const auto expected = std::string(argv[2]);
        if (expected != "valid" && expected != "retained") {
            try {
                auto invalid = gengine::Game(bytes);
            } catch (const std::exception& error) {
                Require(std::string(error.what()).find(expected) != std::string::npos, "unexpected diagnostic");
                return 0;
            }
            throw std::runtime_error("invalid interaction contract was accepted");
        }

        auto game = gengine::Game(bytes);
        Require(game.HasInteractions(), "interaction API was not resolved");
        Require(game.Choose(game.interaction().request, 0) == false, "title accepted selection");
        game.Begin();
        const auto request = game.interaction();
        Require(request.count == 3, "missing options");
        game.Advance(10, {});
        Require(game.state().tick == 0 && game.state().dropped_seconds == 0, "selection advanced time");
        Require(game.Choose(request.request, 3) == false, "invalid option accepted");
        game.Pause(true);
        Require(game.Choose(request.request, 0) == false, "pause accepted selection");
        game.Pause(false);
        const auto selected = game.Choose(request.request, 1);
        if (expected == "retained") {
            Require(selected == false && game.state().phase == gengine::Phase::kFaulted,
                    "retained request did not fault");
            return 0;
        }
        Require(selected && game.interaction().request == 0, "selection was not consumed");
        Require(game.Choose(request.request, 1) == false, "duplicate selection accepted");
        game.Advance(1.0 / 120, {});
        Require(game.state().tick == 0, "selection retained elapsed time");
        game.Reset();
        game.Begin();
        Require(game.interaction().request != request.request && game.Choose(request.request, 0) == false,
                "previous run's selection was accepted");
        Require(game.Choose(game.interaction().request, 2), "restart selection failed");
        game.Step({});
        Require(game.state().tick == 1, "restart did not resume simulation");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
