#include "input_router.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <windows.h>
#include <xinput.h>

namespace gengine
{
    const InputSettings& InputRouter::settings() const
    {
        return settings_;
    }
    void InputRouter::SetSettings(const InputSettings& settings)
    {
        if (ValidInputSettings(settings) == false) {
            throw std::invalid_argument("invalid input settings");
        }
        Clear();
        settings_ = settings;
    }
    bool InputRouter::BindKey(InputAction action, std::uint32_t key)
    {
        const auto index = static_cast<std::uint32_t>(action);
        if (index >= settings_.keys.size() || BindableKey(key) == false) {
            return false;
        }
        auto next = settings_;
        const auto other = std::find(next.keys.begin(), next.keys.end(), key);
        if (other != next.keys.end()) {
            *other = next.keys[index];
        }
        next.keys[index] = key;
        SetSettings(next);
        return true;
    }
    bool InputRouter::BindButton(std::uint32_t action, std::uint32_t button)
    {
        if (action >= settings_.buttons.size() || BindableButton(button) == false) {
            return false;
        }
        auto next = settings_;
        const auto other = std::find(next.buttons.begin(), next.buttons.end(), button);
        if (other != next.buttons.end()) {
            *other = next.buttons[action];
        }
        next.buttons[action] = button;
        SetSettings(next);
        return true;
    }
    bool InputRouter::Key(std::uint32_t key, bool down)
    {
        if (key >= held_.size()) {
            return false;
        }
        if (down == false) {
            held_[key] = false;
            blocked_[key] = false;
            return false;
        }
        if (blocked_[key] || held_[key]) {
            return false;
        }
        held_[key] = true;
        using_pad_ = false;
        if (key == settings_.keys[static_cast<std::uint32_t>(InputAction::kPause)]) {
            pause_key_pressed_ = true;
        }
        return true;
    }
    bool InputRouter::TakeKeyPause()
    {
        const auto pressed = pause_key_pressed_;
        pause_key_pressed_ = false;
        return pressed;
    }
    void InputRouter::Clear(bool block_held)
    {
        // 押下中のキーはkeyupまで遮断し、場面をまたいだ再発火を防ぐ。
        for (auto index = 0u; index < held_.size(); ++index) {
            blocked_[index] = block_held && (blocked_[index] || held_[index]);
        }
        held_.fill(false);
        pause_key_pressed_ = false;
        pad_ready_ = false;
        previous_buttons_ = 0;
        pressed_button_ = 0;
        menu_direction_ = 0;
        repeat_ = 0;
    }
    InputFrame InputRouter::Poll(const PadSample& sample, double seconds, bool enabled)
    {
        auto result = InputFrame{};
        result.disconnected = pad_connected_ && (sample.connected == false || sample.index != pad_index_) && using_pad_;
        if (sample.connected != pad_connected_ || sample.index != pad_index_) {
            pad_ready_ = false;
            previous_buttons_ = 0;
            menu_direction_ = 0;
        }
        pad_connected_ = sample.connected;
        pad_index_ = sample.index;
        pressed_button_ = 0;
        if (enabled == false) {
            Clear();
            return result;
        }

        const auto key = [&](InputAction action) {
            return held_[settings_.keys[static_cast<std::uint32_t>(action)]];
        };
        result.x = float(key(InputAction::kRight)) - float(key(InputAction::kLeft));
        result.y = float(key(InputAction::kUp)) - float(key(InputAction::kDown));
        result.focus = key(InputAction::kFocus);
        result.descend = key(InputAction::kDescend);
        result.ascend = key(InputAction::kAscend);
        result.pause_pressed = TakeKeyPause();
        if (sample.connected == false) {
            return result;
        }

        auto buttons = sample.buttons & 0xf3ffu;
        if (sample.left_trigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
            buttons |= kPadLeftTrigger;
        }
        if (sample.right_trigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
            buttons |= kPadRightTrigger;
        }
        const auto magnitude = std::hypot(float(sample.x), float(sample.y));
        const auto deadzone = float(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        // 再接続・画面遷移の直後は、スティックと全ボタンを離してから受け付ける。
        if (pad_ready_ == false) {
            pad_ready_ = buttons == 0 && magnitude <= deadzone;
            return result;
        }
        const auto pressed = buttons & ~previous_buttons_;
        previous_buttons_ = buttons;
        const auto bindable = pressed & 0x3f3f0u;
        if (BindableButton(bindable)) {
            pressed_button_ = bindable;
        }
        auto x = 0.f;
        auto y = 0.f;
        if (magnitude > deadzone) {
            const auto speed = std::min(1.f, (magnitude - deadzone) / (32767.f - deadzone));
            x = float(sample.x) / magnitude * speed;
            y = float(sample.y) / magnitude * speed;
        }
        if ((buttons & 0xf) != 0) {
            x = float((buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0) - float((buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
            y = float((buttons & XINPUT_GAMEPAD_DPAD_UP) != 0) - float((buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
        }
        if (buttons != 0 || x != 0 || y != 0) {
            using_pad_ = true;
        }
        if (key(InputAction::kLeft) == false && key(InputAction::kRight) == false && key(InputAction::kUp) == false &&
            key(InputAction::kDown) == false) {
            result.x = x;
            result.y = y;
        }
        result.focus = result.focus || (buttons & settings_.buttons[0]) != 0;
        result.pause_pressed = result.pause_pressed || (pressed & settings_.buttons[1]) != 0;
        result.descend = result.descend || (buttons & settings_.buttons[2]) != 0;
        result.ascend = result.ascend || (buttons & settings_.buttons[3]) != 0;

        // メニューの方向は押し始めと長押しの反復だけを通知する。
        const auto direction = y > .5f ? VK_UP : y < -.5f ? VK_DOWN : x < -.5f ? VK_LEFT : x > .5f ? VK_RIGHT : 0u;
        repeat_ -= std::clamp(seconds, 0., .25);
        if (direction != 0 && (direction != menu_direction_ || repeat_ <= 0)) {
            result.menu_keys[result.menu_count++] = direction;
            repeat_ = direction != menu_direction_ ? .35 : .12;
        }
        menu_direction_ = direction;
        if ((pressed & XINPUT_GAMEPAD_A) != 0) {
            result.menu_keys[result.menu_count++] = VK_RETURN;
        }
        if ((pressed & XINPUT_GAMEPAD_B) != 0) {
            result.menu_keys[result.menu_count++] = VK_ESCAPE;
        }
        return result;
    }
    bool InputRouter::using_pad() const
    {
        return using_pad_;
    }
    bool InputRouter::pad_connected() const
    {
        return pad_connected_;
    }
    std::uint32_t InputRouter::pressed_button() const
    {
        return pressed_button_;
    }

    PadSample GamepadDevice::Poll()
    {
        const auto read = [](std::uint32_t index) {
            auto state = XINPUT_STATE{};
            if (XInputGetState(index, &state) != ERROR_SUCCESS) {
                return PadSample{};
            }
            return PadSample{true,
                             index,
                             state.Gamepad.wButtons,
                             state.Gamepad.sThumbLX,
                             state.Gamepad.sThumbLY,
                             state.Gamepad.bLeftTrigger,
                             state.Gamepad.bRightTrigger};
        };
        if (index_) {
            const auto result = read(*index_);
            if (result.connected) {
                return result;
            }
            index_.reset();
            search_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            return {};
        }
        if (std::chrono::steady_clock::now() < search_) {
            return {};
        }
        search_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        for (auto index = 0u; index < XUSER_MAX_COUNT; ++index) {
            const auto result = read(index);
            if (result.connected) {
                index_ = index;
                return result;
            }
        }
        return {};
    }
} // namespace gengine
