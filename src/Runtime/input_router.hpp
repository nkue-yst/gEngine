#pragma once
#include "input_settings.hpp"
#include <chrono>
#include <optional>

namespace gengine
{
    struct PadSample
    {
        bool connected = false;
        std::uint32_t index = 0;
        std::uint32_t buttons = 0;
        std::int16_t x = 0;
        std::int16_t y = 0;
        std::uint8_t left_trigger = 0;
        std::uint8_t right_trigger = 0;
    };
    struct InputFrame
    {
        float x = 0;
        float y = 0;
        bool focus = false;
        bool descend = false;
        bool ascend = false;
        bool pause_pressed = false;
        bool disconnected = false;
        // UI用の仮想キー。ゲームの再割当てと切り離し、常に戻れる操作を維持する。
        std::array<std::uint32_t, 3> menu_keys{};
        std::uint32_t menu_count = 0;
    };
    class InputRouter final
    {
      public:
        const InputSettings& settings() const;
        void SetSettings(const InputSettings& settings);
        // 競合する割当ては交換し、必須アクションを無効にしない。
        bool BindKey(InputAction action, std::uint32_t key);
        bool BindButton(std::uint32_t action, std::uint32_t button);
        bool Key(std::uint32_t key, bool down);
        // 即時にUIへ渡したキーのPauseを、次のフレームで重複させない。
        bool TakeKeyPause();
        void Clear(bool block_held = true);
        InputFrame Poll(const PadSample& sample, double seconds, bool enabled = true);
        bool using_pad() const;
        bool pad_connected() const;
        std::uint32_t pressed_button() const;

      private:
        InputSettings settings_;
        std::array<bool, 256> held_{};
        std::array<bool, 256> blocked_{};
        bool pause_key_pressed_ = false;
        bool pad_ready_ = false;
        bool pad_connected_ = false;
        bool using_pad_ = false;
        std::uint32_t pad_index_ = 0;
        std::uint32_t previous_buttons_ = 0;
        std::uint32_t pressed_button_ = 0;
        std::uint32_t menu_direction_ = 0;
        double repeat_ = 0;
    };
    // 接続中だけ毎フレーム照会し、未接続のポート探索は500ms間隔に抑える。
    class GamepadDevice final
    {
      public:
        PadSample Poll();

      private:
        std::optional<std::uint32_t> index_;
        std::chrono::steady_clock::time_point search_{};
    };
} // namespace gengine
