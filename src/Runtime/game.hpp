#pragma once
#include "performance.hpp"
#include "profile.hpp"
#include "scene_document.hpp"
#include <array>
#include <gvm/gvm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gengine
{
    enum class GameMode
    {
        k2d,
        k3d
    };
    enum class Phase
    {
        kStart,
        kPlaying,
        kResult,
        kFaulted
    };
    struct InputState
    {
        bool left = false;
        bool right = false;
        bool up = false;
        bool down = false;
        bool fire = false;
        bool descend = false;
        bool ascend = false;
        // キーボードで指定していない軸へ、-1～1のアナログ移動を渡す。
        float horizontal = 0;
        float vertical = 0;
    };
    // ラベルは同梱翻訳のID。maximumが0なら分母を表示しない。
    struct StatusValue
    {
        std::uint32_t label = 0;
        std::int32_t value = 0;
        std::int32_t maximum = 0;
    };
    // 場面と短時間の案内を数値IDで渡す。文字・画像の意味はゲーム素材側で定める。
    struct PresentationState
    {
        std::uint32_t scene = 0;
        std::uint32_t cue = 0;
        float cue_seconds = 0;
    };
    // 0なら通常表示。1～3のときだけ同じ世界を指定の窓で描く。VMや音声は増やさない。
    struct WindowViews
    {
        std::uint32_t count = 0;
        float scale = 1;
        gvm::Vec2 size{16, 9};
        std::array<gvm::Vec2, 3> centers{};
    };
    struct GameState
    {
        Phase phase = Phase::kStart;
        bool paused = false;
        std::uint32_t preset = 0;
        std::uint64_t tick = 0;
        std::uint32_t outcome = 0;
        std::uint32_t dropped_shots = 0;
        std::int32_t hp = 3;
        std::int32_t enemy_hp = 30;
        gvm::Vec3 player{0, -3, 0};
        double dropped_seconds = 0;
        std::string diagnostic;
        std::uint64_t update_ns = 0;
        std::uint64_t contact_ns = 0;
        std::uint64_t apply_ns = 0;
        std::uint64_t owned_bytes = 0;
        std::uint64_t peak_owned_bytes = 0;
        std::uint64_t total_update_ns = 0;
        std::uint64_t total_contact_ns = 0;
        std::uint64_t total_apply_ns = 0;
        std::uint32_t status_count = 0;
        std::array<StatusValue, 4> status{};
        PresentationState presentation;
        WindowViews windows;
    };
    // 選択中は戦闘時間を止める。requestは古い画面からの二重決定を拒否する識別子。
    struct InteractionState
    {
        std::uint64_t request = 0;
        std::uint32_t count = 0;
        std::array<std::uint32_t, 3> options{};
    };
    struct Dot
    {
        float x;
        float y;
        float z;
        bool hostile;
    };
    // maskのbit 0は自機弾、bit 1は自機との接触。visual 0は標準、1はシーンの敵素材。
    // 2Dのvisual 2～17はatlas、API 2の18～33はdetail。3DはAPI 1の0・1だけを受け付ける。
    struct Actor
    {
        gvm::Vec3 previous;
        gvm::Vec3 position;
        gvm::Vec3 color;
        float radius;
        float angle;
        std::uint32_t visual;
        std::uint32_t mask;
        float opacity = 1;
    };
    // VMの更新と接触反映を一つの所有スレッドで行う。描画配列は毎回作り直す。
    class Game final
    {
      public:
        explicit Game(std::span<const std::uint8_t> bytes, std::uint64_t step_fuel = 1000000,
                      GameMode mode = GameMode::k2d, std::optional<SceneDocument> scene = {});
        void Reset(std::uint32_t preset = 0);
        void Begin();
        void Pause(bool paused);
        void Fail(std::string diagnostic);
        void Advance(double seconds, const InputState& input);
        void Step(const InputState& input);
        const InteractionState& interaction() const
        {
            return interaction_;
        }
        bool HasInteractions() const
        {
            return interaction_request_.has_value();
        }
        bool HasUi() const
        {
            return HasInteractions() || status_count_.has_value() || HasProfile();
        }
        bool HasProfile() const
        {
            return profile_schema_.has_value();
        }
        const ProfileData& profile() const
        {
            return profile_;
        }
        // タイトルでのみ復元・購入を受け付ける。通常のResetでは永続値を保持する。
        bool RestoreProfile(const ProfileData& value);
        std::vector<ProfileAction> ProfileActions();
        bool ActOnProfile(std::uint32_t index);
        bool HasProfileReset() const
        {
            return profile_reset_.has_value();
        }
        // タイトルの明示操作だけで進行値を初期化する。設定や保存ファイルはこのAPIで消さない。
        bool ResetProfile();
        bool HasActors() const
        {
            return actor_hit_.has_value();
        }
        bool HasWindowViews() const
        {
            return window_count_.has_value();
        }
        const std::vector<Actor>& actors() const
        {
            return actors_;
        }
        // 現在表示した要求と0始まりの選択位置を渡す。Pause中・古い要求・範囲外は変更しない。
        bool Choose(std::uint64_t request, std::uint32_t index);
        const GameState& state() const
        {
            return state_;
        }
        const std::vector<Dot>& Dots();
        std::uint32_t TakeSounds();
        // 未宣言なら音源定義の既定曲。宣言時の0は無音、1～8は曲の番号。
        std::optional<std::uint32_t> music_track() const
        {
            return music_track_;
        }
        std::vector<PerformanceCounter> MemoryCounters();
        GameMode mode() const
        {
            return mode_;
        }
        gvm::Vec3 EnemyPosition() const;
        const SceneDocument& scene() const
        {
            return scene_;
        }
        // 2DはZ=0として、移動する円・球の判定を同じ計算へ通す。
        static bool Contact(gvm::Vec3 from, gvm::Vec3 to, gvm::Vec3 target_from, gvm::Vec3 target_to, float radius);

      private:
        void ResolveProfile();
        void ApplyProfile();
        void CaptureProfile();
        void Refresh();
        void RefreshInteraction();
        void RefreshActors();
        void FindActorContacts(gvm::Vec3 previous, gvm::Vec3 player);
        void ApplyActorContacts();
        gvm::Vec3 PlayerPosition(bool previous = false);
        gvm::Vm vm_;
        GameMode mode_;
        SceneDocument scene_;
        gvm::Function<void(std::uint32_t)> initialize_;
        std::optional<gvm::Function<void(gvm::Vec2, bool, float)>> step2_;
        std::optional<gvm::Function<void(gvm::Vec3, bool, float)>> step3_;
        gvm::Function<void(std::uint32_t, std::uint32_t, std::uint32_t)> hits_;
        gvm::Function<void()> finish_;
        std::optional<gvm::Function<gvm::Vec2()>> position2_;
        std::optional<gvm::Function<gvm::Vec2()>> previous2_;
        std::optional<gvm::Function<gvm::Vec3()>> position3_;
        std::optional<gvm::Function<gvm::Vec3()>> previous3_;
        gvm::Function<std::int32_t()> hp_;
        gvm::Function<std::int32_t()> target_hp_;
        gvm::Function<std::uint32_t()> result_;
        gvm::Function<std::uint32_t()> sounds_;
        std::optional<gvm::Function<std::uint32_t()>> music_function_;
        std::optional<gvm::Function<std::uint32_t()>> presentation_scene_;
        std::optional<gvm::Function<std::uint32_t()>> presentation_cue_;
        std::optional<gvm::Function<float()>> presentation_cue_seconds_;
        bool actor_opacity_ = false;
        std::uint32_t preset_count_ = 5;
        std::optional<gvm::Function<std::uint32_t()>> window_count_;
        std::optional<gvm::Function<float()>> window_scale_;
        std::optional<gvm::Function<gvm::Vec2()>> window_size_;
        std::optional<gvm::Function<gvm::Vec2(std::uint32_t)>> window_center_;
        std::optional<std::uint32_t> music_track_;
        gvm::Function<std::uint32_t()> dropped_;
        std::optional<gvm::Function<std::uint32_t()>> interaction_request_;
        std::optional<gvm::Function<std::uint32_t()>> interaction_count_;
        std::optional<gvm::Function<std::uint32_t(std::uint32_t)>> interaction_option_;
        std::optional<gvm::Function<bool(std::uint32_t)>> interaction_choose_;
        InteractionState interaction_;
        std::uint32_t interaction_token_ = 0;
        std::uint64_t interaction_serial_ = 0;
        std::optional<gvm::Function<void(std::uint32_t, std::uint32_t)>> actor_hit_;
        std::optional<gvm::Function<void(std::uint32_t)>> actor_contact_;
        std::optional<gvm::Function<std::uint32_t()>> status_count_;
        std::optional<gvm::Function<std::uint32_t(std::uint32_t)>> status_label_;
        std::optional<gvm::Function<std::int32_t(std::uint32_t)>> status_value_;
        std::optional<gvm::Function<std::int32_t(std::uint32_t)>> status_maximum_;
        std::vector<Actor> actors_;
        std::vector<std::array<std::uint32_t, 2>> actor_hits_;
        std::vector<std::uint32_t> actor_contacts_;
        ProfileData profile_;
        std::optional<gvm::Function<std::uint32_t()>> profile_schema_;
        std::optional<gvm::Function<std::uint32_t()>> profile_count_;
        std::optional<gvm::Function<std::uint32_t(std::uint32_t)>> profile_get_;
        std::optional<gvm::Function<bool(std::uint32_t, std::uint32_t)>> profile_set_;
        std::optional<gvm::Function<bool(std::uint32_t, std::uint32_t)>> profile_accept_;
        std::optional<gvm::Function<std::uint32_t()>> profile_action_count_;
        std::optional<gvm::Function<std::uint32_t(std::uint32_t)>> profile_action_label_;
        std::optional<gvm::Function<std::uint32_t(std::uint32_t)>> profile_action_rank_;
        std::optional<gvm::Function<std::uint32_t(std::uint32_t)>> profile_action_maximum_;
        std::optional<gvm::Function<std::uint32_t(std::uint32_t)>> profile_action_cost_;
        std::optional<gvm::Function<bool(std::uint32_t)>> profile_action_enabled_;
        std::optional<gvm::Function<bool(std::uint32_t)>> profile_action_;
        std::optional<gvm::Function<bool()>> profile_reset_;
        GameState state_;
        double accumulator_ = 0;
        std::uint32_t pending_sounds_ = 0;
        std::vector<Dot> dots_;
        std::uint64_t step_fuel_;
    };
} // namespace gengine
