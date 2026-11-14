#pragma once
#include "assets.hpp"
#include "game.hpp"
#include "game_audio.hpp"
#include "input_router.hpp"
#include "window_display.hpp"
#include <rengine/renderer.hpp>

namespace gengine
{
    enum class UiCommand
    {
        kUnhandled,
        kHandled,
        kBegin,
        kPause,
        kResume,
        kAbandon,
        kChosen,
        kClose
    };
    // ゲームが同梱する翻訳とフォントatlasを、PIEとRunnerで共用する。
    class GameUi final
    {
      public:
        GameUi(rengine::Renderer& renderer, const AssetSource& source, AssetMetrics& metrics);
        ~GameUi();
        void SwitchLanguage();
        void SetLanguage(std::uint32_t language);
        std::uint32_t language() const;
        const std::string& profile_id() const;
        bool HasDisplaySettings() const;
        void SetDisplay(WindowDisplay* display);
        bool HasAudioSettings() const;
        void SetAudio(GameAudio* audio);
        bool HasInputSettings() const;
        void SetInput(InputRouter* input);
        bool CapturingInput() const;
        bool PadBinding(std::uint32_t button);
        void SetPresentation(bool enabled);
        bool TitleMenuVisible(const Game& game) const;
        void ShowPause();
        UiCommand TogglePause(Game& game);
        bool RequestClose(Game& game);
        void ResetMenu();
        UiCommand Key(Game& game, std::uint32_t key);
        UiCommand Click(Game& game, float x, float y);
        // 描画した操作領域を使い、ホバーと音量のドラッグを処理する。
        UiCommand PointerMove(Game& game, float x, float y);
        void ReleasePointer();
        bool PointerActive() const;
        rengine::Draw Draw(Game& game, SaveStatus save_status = SaveStatus::kMemory);

      private:
        UiCommand AdvancedKey(Game& game, std::uint32_t key);
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace gengine
