#include "input_router.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool value, const char* reason)
    {
        if (value == false) {
            throw std::runtime_error(reason);
        }
    }
} // namespace
int main()
{
    try {
        auto input = gengine::InputRouter{};
        Require(gengine::ValidInputSettings(input.settings()), "invalid defaults");
        Require(input.Key('W', true), "key edge missing");
        Require(input.Poll({}, .016).y == 1, "movement missing");
        input.Clear();
        Require(input.Key('W', true) == false && input.Poll({}, .016).y == 0, "held key survived transition");
        input.Key('W', false);
        Require(input.Key('W', true) && input.Poll({}, .016).y == 1, "released key did not reactivate");
        input.Key('W', false);
        Require(input.BindKey(gengine::InputAction::kUp, 'A') && input.settings().keys[0] == 'W' &&
                    input.settings().keys[2] == 'A' && gengine::ValidInputSettings(input.settings()),
                "conflicting binding lost an action");
        Require(input.BindKey(gengine::InputAction::kUp, 0x1b) == false, "Escape was rebound");
        input.Key('P', true);
        Require(input.Poll({}, .016).pause_pressed && input.Poll({}, .016).pause_pressed == false,
                "pause was repeated");
        input.Key('P', false);

        // 接続直後に押されていた決定やスティックは、一度離すまで無視する。
        auto pad = gengine::PadSample{true, 2, 0x1000, 32767, 0};
        auto frame = input.Poll(pad, .016);
        Require(frame.x == 0 && frame.menu_count == 0, "hotplug carried input");
        pad.buttons = 0;
        pad.x = 0;
        input.Poll(pad, .016);
        pad.x = 7000;
        Require(input.Poll(pad, .016).x == 0, "deadzone drifted");
        pad.x = 17000;
        frame = input.Poll(pad, .016);
        Require(frame.x > .3f && frame.x < .5f, "analog magnitude discarded");
        pad.x = -32768;
        pad.y = -32768;
        frame = input.Poll(pad, .016);
        Require(std::abs(std::hypot(frame.x, frame.y) - 1) < .0001f, "diagonal exceeded unit speed");
        pad.x = 0;
        pad.y = 0;
        pad.buttons = 0x1001;
        frame = input.Poll(pad, .016);
        Require(frame.menu_count == 2 && frame.menu_keys[0] == 0x26 && frame.menu_keys[1] == 0x0d,
                "controller menu direction/confirm missing");
        Require(input.Poll(pad, .1).menu_count == 0, "held confirmation repeated");
        Require(input.Poll(pad, .25).menu_count == 1, "direction repeat missing");
        input.Clear();
        Require(input.Poll(pad, .5).menu_count == 0, "controller survived transition");
        pad.buttons = 0;
        input.Poll(pad, .016);
        pad.right_trigger = 31;
        frame = input.Poll(pad, .016);
        Require(frame.focus && input.pressed_button() == gengine::kPadRightTrigger, "trigger input missing");
        Require(input.Poll({}, .016).disconnected, "active controller disconnect missing");
        Require(input.Poll({}, .016).disconnected == false, "disconnect repeated");

        pad.right_trigger = 0;
        input.Poll(pad, .016);
        pad.buttons = 0x10;
        Require(input.Poll(pad, .016).pause_pressed, "controller Pause missing");
        Require(input.Poll(pad, .016).pause_pressed == false, "controller Pause repeated");
        Require(input.BindButton(0, 0x10) && input.settings().buttons[1] == gengine::kPadRightTrigger,
                "controller conflict did not swap");
        Require(input.BindButton(0, 0x1001) == false, "multiple buttons accepted as one binding");

        // APIの実接続も照会する。未接続を実機操作の合格とは報告しない。
        auto device = gengine::GamepadDevice{};
        const auto detected = device.Poll();
        std::cout
            << "input edges, remap conflicts, deadzone, repeat, transitions and disconnect passed; physical_controller="
            << detected.connected << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
