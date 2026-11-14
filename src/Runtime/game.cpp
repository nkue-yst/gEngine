#include "game.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace gengine
{
    Game::Game(std::span<const std::uint8_t> bytes, std::uint64_t step_fuel, GameMode mode,
               std::optional<SceneDocument> scene)
        : vm_(bytes), mode_(mode), scene_(scene.value_or(DefaultScene(mode == GameMode::k3d))),
          initialize_(vm_.ResolveFunction<void(std::uint32_t)>("initialize")),
          hits_(vm_.ResolveFunction<void(std::uint32_t, std::uint32_t, std::uint32_t)>("apply_hits")),
          finish_(vm_.ResolveFunction<void()>("finish_step")), hp_(vm_.ResolveFunction<std::int32_t()>("player_hp")),
          target_hp_(vm_.ResolveFunction<std::int32_t()>("target_hp")),
          result_(vm_.ResolveFunction<std::uint32_t()>("result")),
          sounds_(vm_.ResolveFunction<std::uint32_t()>("audio_events")),
          dropped_(vm_.ResolveFunction<std::uint32_t()>("dropped_shots")), step_fuel_(step_fuel)
    {
        // 選択した次元と公開関数の型を照合し、異なる次元のモジュールを拒否する。
        if (mode_ == GameMode::k3d) {
            step3_ = vm_.ResolveFunction<void(gvm::Vec3, bool, float)>("step");
            position3_ = vm_.ResolveFunction<gvm::Vec3()>("player_position");
            previous3_ = vm_.ResolveFunction<gvm::Vec3()>("player_previous");
        }
        else if (mode_ == GameMode::k2d) {
            step2_ = vm_.ResolveFunction<void(gvm::Vec2, bool, float)>("step");
            position2_ = vm_.ResolveFunction<gvm::Vec2()>("player_position");
            previous2_ = vm_.ResolveFunction<gvm::Vec2()>("player_previous");
        }
        else {
            throw std::invalid_argument("unknown game mode");
        }
        ValidateSceneForPlay(scene_);
        if ((scene_.game == "3d") != (mode_ == GameMode::k3d)) {
            throw std::invalid_argument("scene and module dimension mismatch");
        }
        dots_.reserve(8192);
        // 版宣言のない既存モジュールは従来どおり動かす。型不一致や不完全な宣言は拒否する。
        const auto optional_version = [this](std::string_view name) {
            auto function = std::optional<gvm::Function<std::uint32_t()>>{};
            try {
                function = vm_.ResolveFunction<std::uint32_t()>(name);
            } catch (const gvm::Error& error) {
                if (std::string_view(error.what()) != "ExportNotFound") {
                    throw;
                }
            }
            return function;
        };
        const auto interaction_version = optional_version("interaction_api_version");
        if (const auto version = optional_version("window_api_version")) {
            if (mode_ != GameMode::k2d || vm_.CallWithFuel(step_fuel_, *version) != 1) {
                throw std::invalid_argument("window API version 1 requires a 2D game");
            }
            window_count_ = vm_.ResolveFunction<std::uint32_t()>("window_view_count");
            window_scale_ = vm_.ResolveFunction<float()>("window_scale");
            window_size_ = vm_.ResolveFunction<gvm::Vec2()>("window_view_size");
            window_center_ = vm_.ResolveFunction<gvm::Vec2(std::uint32_t)>("window_view_center");
        }
        if (const auto count = optional_version("play_preset_count")) {
            preset_count_ = vm_.CallWithFuel(step_fuel_, *count);
            if (preset_count_ == 0 || preset_count_ > 32) {
                throw std::invalid_argument("play preset count must be 1..32");
            }
        }
        music_function_ = optional_version("music_track");
        if (interaction_version) {
            if (vm_.CallWithFuel(step_fuel_, *interaction_version) != 1) {
                throw std::invalid_argument("unsupported interaction API version");
            }
            interaction_request_ = vm_.ResolveFunction<std::uint32_t()>("interaction_request");
            interaction_count_ = vm_.ResolveFunction<std::uint32_t()>("interaction_count");
            interaction_option_ = vm_.ResolveFunction<std::uint32_t(std::uint32_t)>("interaction_option");
            interaction_choose_ = vm_.ResolveFunction<bool(std::uint32_t)>("interaction_choose");
        }
        if (const auto actor_version = optional_version("actor_api_version")) {
            const auto version = vm_.CallWithFuel(step_fuel_, *actor_version);
            if (version != 1 && version != 2) {
                throw std::invalid_argument("unsupported actor API version");
            }
            actor_opacity_ = version == 2;
            if (actor_opacity_ && mode_ != GameMode::k2d) {
                throw std::invalid_argument("actor API 2 requires a 2D game");
            }
            actor_hit_ = vm_.ResolveFunction<void(std::uint32_t, std::uint32_t)>("actor_apply_hit");
            actor_contact_ = vm_.ResolveFunction<void(std::uint32_t)>("actor_apply_contact");
        }
        if (const auto status_version = optional_version("status_api_version")) {
            if (vm_.CallWithFuel(step_fuel_, *status_version) != 1) {
                throw std::invalid_argument("unsupported status API version");
            }
            status_count_ = vm_.ResolveFunction<std::uint32_t()>("status_count");
            status_label_ = vm_.ResolveFunction<std::uint32_t(std::uint32_t)>("status_label");
            status_value_ = vm_.ResolveFunction<std::int32_t(std::uint32_t)>("status_value");
            status_maximum_ = vm_.ResolveFunction<std::int32_t(std::uint32_t)>("status_maximum");
        }
        if (const auto presentation_version = optional_version("presentation_api_version")) {
            if (vm_.CallWithFuel(step_fuel_, *presentation_version) != 1) {
                throw std::invalid_argument("unsupported presentation API version");
            }
            presentation_scene_ = vm_.ResolveFunction<std::uint32_t()>("presentation_scene");
            presentation_cue_ = vm_.ResolveFunction<std::uint32_t()>("presentation_cue");
            presentation_cue_seconds_ = vm_.ResolveFunction<float()>("presentation_cue_seconds");
        }
        // 更新前に必要な列と容量を照合し、不適合なゲームを画面へ接続しない。
        for (const auto* name : {"shots", "threats"}) {
            auto view = vm_.InspectPool(name);
            if (view.capacity() > 4096) {
                throw std::invalid_argument("game pool capacity exceeds 4096");
            }
            for (const auto* field : {"position", "previous"}) {
                view.Column<float>(field, 0);
                view.Column<float>(field, 1);
                if (mode_ == GameMode::k3d) {
                    view.Column<float>(field, 2);
                }
            }
        }
        if (HasActors()) {
            const auto capacity = vm_.InspectPool("actors").capacity();
            if (capacity > 1024) {
                throw std::invalid_argument("actor pool exceeds 1024");
            }
            // 上限全体ではなく、このモジュールの固定容量だけを事前確保する。
            actors_.reserve(capacity);
            actor_contacts_.reserve(capacity);
            actor_hits_.reserve(vm_.InspectPool("shots").capacity());
        }
        ResolveProfile();
        Reset();
    }

    void Game::Reset(std::uint32_t preset)
    {
        if (preset >= preset_count_) {
            throw std::invalid_argument("unknown play preset");
        }
        vm_.Reset();
        state_ = {};
        interaction_ = {};
        interaction_token_ = 0;
        actors_.clear();
        actor_hits_.clear();
        actor_contacts_.clear();
        state_.preset = preset;
        accumulator_ = 0;
        pending_sounds_ = 0;
        // 同じスナップショットを毎回適用し、再試行や通常開始で編集値を失わない。
        const auto& settings = scene_.settings;
        const auto player = std::find_if(scene_.objects.begin(), scene_.objects.end(), [](const auto& o) {
            return o.kind == "player_spawn";
        });
        const auto enemy = std::find_if(scene_.objects.begin(), scene_.objects.end(), [](const auto& o) {
            return o.kind == "enemy_spawn";
        });
        try {
            if (mode_ == GameMode::k3d) {
                const auto vector = [](const auto& v) {
                    return gvm::Vec3{v[0], v[1], v[2]};
                };
                const auto configure =
                    vm_.ResolveFunction<void(gvm::Vec3, gvm::Vec3, gvm::Vec3, gvm::Vec3)>("configure_world");
                vm_.Call(configure, vector(player->position), vector(enemy->position), vector(settings.bounds_min),
                         vector(settings.bounds_max));
            }
            else {
                const auto vector = [](const auto& v) {
                    return gvm::Vec2{v[0], v[1]};
                };
                const auto configure =
                    vm_.ResolveFunction<void(gvm::Vec2, gvm::Vec2, gvm::Vec2, gvm::Vec2)>("configure_world");
                vm_.Call(configure, vector(player->position), vector(enemy->position), vector(settings.bounds_min),
                         vector(settings.bounds_max));
            }
            const auto rules =
                vm_.ResolveFunction<void(std::int32_t, std::int32_t, float, float, float)>("configure_rules");
            vm_.Call(rules, settings.player_hp, settings.enemy_hp, settings.move_speed, settings.fire_interval_seconds,
                     enemy->fire_interval_seconds);
        } catch (const gvm::Error&) {
            throw std::runtime_error(
                "scene initialization API missing or invalid; recompile the game source for gEngine 0.0.8");
        }
        ApplyProfile();
        vm_.Call(initialize_, preset);
        CaptureProfile();
        Refresh();
        auto stats = GvmStats{};
        if (gvm_get_stats(vm_.native_handle(), &stats) != GVM_OK) {
            throw std::runtime_error("cannot read game VM memory");
        }
        state_.owned_bytes = stats.owned_bytes;
        state_.peak_owned_bytes = stats.peak_owned_bytes;
        if (preset != 0) {
            state_.phase = Phase::kPlaying;
        }
    }

    void Game::Begin()
    {
        const auto preset = state_.preset;
        Reset(preset);
        state_.phase = Phase::kPlaying;
    }

    void Game::Pause(bool paused)
    {
        state_.paused = paused;
        accumulator_ = 0;
    }

    void Game::Fail(std::string diagnostic)
    {
        pending_sounds_ = 0;
        interaction_ = {};
        actors_.clear();
        state_.phase = Phase::kFaulted;
        state_.diagnostic = std::move(diagnostic);
    }

    bool Game::Contact(gvm::Vec3 from, gvm::Vec3 to, gvm::Vec3 target_from, gvm::Vec3 target_to, float radius)
    {
        // 標的から見た弾の移動区間で最接近点を求め、高速ですり抜ける接触も拾う。
        const auto x = from.x - target_from.x;
        const auto y = from.y - target_from.y;
        const auto z = from.z - target_from.z;
        const auto dx = to.x - target_to.x - x;
        const auto dy = to.y - target_to.y - y;
        const auto dz = to.z - target_to.z - z;
        const auto length = dx * dx + dy * dy + dz * dz;
        // 動かない場合は始点だけを比較する。tは一更新内の0～1へ制限する。
        const auto t = length > 0 ? std::clamp(-(x * dx + y * dy + z * dz) / length, 0.0f, 1.0f) : 0.0f;
        return (x + dx * t) * (x + dx * t) + (y + dy * t) * (y + dy * t) + (z + dz * t) * (z + dz * t) <=
               radius * radius;
    }

    gvm::Vec3 Game::EnemyPosition() const
    {
        const auto& p = std::find_if(scene_.objects.begin(), scene_.objects.end(), [](const auto& o) {
                            return o.kind == "enemy_spawn";
                        })->position;
        return {p[0], p[1], mode_ == GameMode::k3d ? p[2] : 0};
    }

    gvm::Vec3 Game::PlayerPosition(bool previous)
    {
        if (mode_ == GameMode::k3d) {
            return vm_.Call(previous ? *previous3_ : *position3_);
        }
        const auto value = vm_.Call(previous ? *previous2_ : *position2_);
        return {value.x, value.y, 0};
    }

    void Game::Refresh()
    {
        state_.player = PlayerPosition();
        state_.hp = vm_.Call(hp_);
        state_.enemy_hp = vm_.Call(target_hp_);
        state_.outcome = vm_.Call(result_);
        state_.dropped_shots = vm_.Call(dropped_);
        if (window_count_) {
            auto& value = state_.windows;
            value.count = vm_.CallWithFuel(step_fuel_, *window_count_);
            value.scale = vm_.CallWithFuel(step_fuel_, *window_scale_);
            value.size = vm_.CallWithFuel(step_fuel_, *window_size_);
            if (value.count > 3 || std::isfinite(value.scale) == false || value.scale < .5f || value.scale > 1 ||
                std::isfinite(value.size.x) == false || std::isfinite(value.size.y) == false || value.size.x < 4 ||
                value.size.x > 64 || value.size.y < 2.25f || value.size.y > 36 ||
                std::abs(value.size.x / value.size.y - 16.f / 9) > .001f) {
                throw std::runtime_error("window view dimensions are out of range");
            }
            for (auto index = 0u; index < value.count; ++index) {
                value.centers[index] = vm_.CallWithFuel(step_fuel_, *window_center_, index);
                const auto center = value.centers[index];
                if (std::isfinite(center.x) == false || std::isfinite(center.y) == false ||
                    std::abs(center.x) > 1000000 || std::abs(center.y) > 1000000) {
                    throw std::runtime_error("window view center is out of range");
                }
            }
        }
        if (presentation_scene_) {
            auto& value = state_.presentation;
            value = {vm_.CallWithFuel(step_fuel_, *presentation_scene_),
                     vm_.CallWithFuel(step_fuel_, *presentation_cue_),
                     vm_.CallWithFuel(step_fuel_, *presentation_cue_seconds_)};
            if (value.scene > 16 || value.cue > 64 || std::isfinite(value.cue_seconds) == false ||
                value.cue_seconds < 0 || value.cue_seconds > 30) {
                throw std::runtime_error("presentation value is out of range");
            }
        }
        if (music_function_) {
            music_track_ = vm_.CallWithFuel(step_fuel_, *music_function_);
            if (*music_track_ > 8) {
                throw std::runtime_error("music track exceeds eight");
            }
        }
        RefreshInteraction();
        RefreshActors();
        // HUDへ渡す値は固定長で保持し、ゲーム固有の経験値や経過時間を解釈しない。
        if (status_count_) {
            state_.status_count = vm_.CallWithFuel(step_fuel_, *status_count_);
            if (state_.status_count > state_.status.size()) {
                throw std::runtime_error("status count exceeds four");
            }
            for (auto index = 0u; index < state_.status_count; ++index) {
                auto& value = state_.status[index];
                value = {vm_.CallWithFuel(step_fuel_, *status_label_, index),
                         vm_.CallWithFuel(step_fuel_, *status_value_, index),
                         vm_.CallWithFuel(step_fuel_, *status_maximum_, index)};
                if (value.label >= 256 || value.maximum < 0) {
                    throw std::runtime_error("status field out of range");
                }
            }
        }
    }

    void Game::RefreshInteraction()
    {
        interaction_ = {};
        if (interaction_request_.has_value() == false || state_.outcome != 0) {
            return;
        }
        auto next = InteractionState{};
        const auto token = vm_.CallWithFuel(step_fuel_, *interaction_request_);
        if (token == 0) {
            interaction_token_ = 0;
            return;
        }
        if (token != interaction_token_) {
            ++interaction_serial_;
        }
        interaction_token_ = token;
        next.request = interaction_serial_;
        next.count = vm_.CallWithFuel(step_fuel_, *interaction_count_);
        if (next.count == 0 || next.count > next.options.size()) {
            throw std::runtime_error("interaction requires one to three options");
        }
        for (auto index = 0u; index < next.count; ++index) {
            next.options[index] = vm_.CallWithFuel(step_fuel_, *interaction_option_, index);
            for (auto earlier = 0u; earlier < index; ++earlier) {
                if (next.options[earlier] == next.options[index]) {
                    throw std::runtime_error("interaction contains duplicate option IDs");
                }
            }
        }
        interaction_ = next;
    }

    bool Game::Choose(std::uint64_t request, std::uint32_t index)
    {
        if (state_.phase != Phase::kPlaying || state_.paused || request == 0 || request != interaction_.request ||
            index >= interaction_.count) {
            return false;
        }
        try {
            if (vm_.CallWithFuel(step_fuel_, *interaction_choose_, index) == false) {
                throw std::runtime_error("game rejected a published interaction option");
            }
            Refresh();
            // 同じ要求を残すと押下の再送で報酬が増えるため、契約違反として停止する。
            if (interaction_.request == request) {
                throw std::runtime_error("game retained a completed interaction request");
            }
            accumulator_ = 0;
            return true;
        } catch (const std::exception& error) {
            Fail(error.what());
            return false;
        }
    }

    void Game::Step(const InputState& input)
    {
        if (state_.phase != Phase::kPlaying || state_.paused || interaction_.request != 0) {
            return;
        }
        try {
            // 3Dは縦の移動を奥行きへ振り分ける。倒し量を保ち、斜めの最大速度だけを揃える。
            if (std::isfinite(input.horizontal) == false || std::isfinite(input.vertical) == false ||
                std::abs(input.horizontal) > 1 || std::abs(input.vertical) > 1) {
                throw std::invalid_argument("analog input requires finite axes in -1..1");
            }
            const auto horizontal =
                input.left || input.right ? float(input.right) - float(input.left) : input.horizontal;
            const auto vertical = input.up || input.down ? float(input.up) - float(input.down) : input.vertical;
            auto direction = gvm::Vec3{horizontal, vertical, 0};
            if (mode_ == GameMode::k3d) {
                direction.y = float(input.ascend) - float(input.descend);
                direction.z = -vertical;
            }
            const auto length =
                std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            if (length > 1) {
                direction.x /= length;
                direction.y /= length;
                direction.z /= length;
            }
            const auto started = std::chrono::steady_clock::now();
            if (mode_ == GameMode::k3d) {
                vm_.CallWithFuel(step_fuel_, *step3_, direction, input.fire, 1.0f / 60.0f);
            }
            else {
                vm_.CallWithFuel(step_fuel_, *step2_, gvm::Vec2{direction.x, direction.y}, input.fire, 1.0f / 60.0f);
            }
            const auto updated = std::chrono::steady_clock::now();
            const auto player = PlayerPosition();
            const auto previous = PlayerPosition(true);
            RefreshActors();
            FindActorContacts(previous, player);
            auto masks = std::array<std::array<std::uint32_t, 128>, 2>{};

            // 両陣営の接触を先に確定する。ビューのスコープ内からVMを呼ばない。
            for (std::uint32_t group = 0; group < 2; ++group) {
                if (group == 0 && HasActors()) {
                    continue;
                }
                auto view = vm_.InspectPool(group == 0 ? "shots" : "threats");
                const auto x = view.Column<float>("position", 0);
                const auto y = view.Column<float>("position", 1);
                const auto px = view.Column<float>("previous", 0);
                const auto py = view.Column<float>("previous", 1);
                const auto z = mode_ == GameMode::k3d ? view.Column<float>("position", 2) : std::span<const float>{};
                const auto pz = mode_ == GameMode::k3d ? view.Column<float>("previous", 2) : std::span<const float>{};
                if (x.size() > 4096) {
                    throw std::runtime_error("game pool exceeds host capacity");
                }
                for (std::size_t row = 0; row < x.size(); ++row) {
                    if (Contact({px[row], py[row], pz.empty() ? 0 : pz[row]}, {x[row], y[row], z.empty() ? 0 : z[row]},
                                group == 0 ? EnemyPosition() : previous, group == 0 ? EnemyPosition() : player,
                                group == 0 ? 0.85f : 0.35f)) {
                        masks[group][row / 32] |= std::uint32_t{1} << (row % 32);
                    }
                }
            }
            const auto contacted = std::chrono::steady_clock::now();
            ApplyActorContacts();
            for (std::uint32_t group = 0; group < 2; ++group) {
                for (std::uint32_t block = 0; block < 128; ++block) {
                    if (masks[group][block] != 0) {
                        vm_.Call(hits_, group, block * 32, masks[group][block]);
                    }
                }
            }
            vm_.Call(finish_);
            Refresh();
            const auto finished = std::chrono::steady_clock::now();
            state_.update_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(updated - started).count());
            state_.contact_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(contacted - updated).count());
            state_.apply_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(finished - contacted).count());
            state_.total_update_ns += state_.update_ns;
            state_.total_contact_ns += state_.contact_ns;
            state_.total_apply_ns += state_.apply_ns;
            pending_sounds_ |= vm_.Call(sounds_);
            ++state_.tick;
            if (state_.outcome != 0) {
                CaptureProfile();
                state_.phase = Phase::kResult;
                pending_sounds_ |= state_.outcome == 1 ? 4u : 8u;
            }
        } catch (const std::exception& error) {
            Fail(error.what());
        }
    }

    void Game::Advance(double seconds, const InputState& input)
    {
        if (std::isfinite(seconds) == false || seconds < 0) {
            throw std::invalid_argument("invalid elapsed time");
        }
        if (state_.paused || state_.phase != Phase::kPlaying || interaction_.request != 0) {
            accumulator_ = 0;
            return;
        }
        accumulator_ += seconds;
        auto steps = 0;
        while (accumulator_ >= 1.0 / 60.0 && steps < 4) {
            Step(input);
            accumulator_ -= 1.0 / 60.0;
            ++steps;
            // 選択へ入ったフレームの残り時間も捨て、決定後に弾が飛び進まないようにする。
            if (interaction_.request != 0 || state_.phase != Phase::kPlaying) {
                accumulator_ = 0;
                break;
            }
        }
        if (accumulator_ >= 1.0 / 60.0) {
            const auto remainder = std::fmod(accumulator_, 1.0 / 60.0);
            state_.dropped_seconds += accumulator_ - remainder;
            accumulator_ = remainder;
        }
    }

    const std::vector<Dot>& Game::Dots()
    {
        dots_.clear();
        if (state_.phase == Phase::kFaulted) {
            return dots_;
        }
        for (auto group = 0; group < 2; ++group) {
            auto view = vm_.InspectPool(group == 0 ? "shots" : "threats");
            const auto x = view.Column<float>("position", 0);
            const auto y = view.Column<float>("position", 1);
            const auto z = mode_ == GameMode::k3d ? view.Column<float>("position", 2) : std::span<const float>{};
            for (std::size_t row = 0; row < x.size(); ++row) {
                dots_.push_back({x[row], y[row], z.empty() ? 0 : z[row], group != 0});
            }
        }
        return dots_;
    }

    std::uint32_t Game::TakeSounds()
    {
        const auto result = pending_sounds_;
        pending_sounds_ = 0;
        return result;
    }

    std::vector<PerformanceCounter> Game::MemoryCounters()
    {
        auto memory = GvmMemoryStats{};
        auto stats = GvmStats{};
        if (gvm_get_memory_stats(vm_.native_handle(), &memory) != GVM_OK ||
            gvm_get_stats(vm_.native_handle(), &stats) != GVM_OK) {
            throw std::runtime_error("cannot read game VM memory");
        }
        auto live = std::uint64_t{0};
        auto count = std::uint64_t{0};
        // 生存列の実データと、VMが保持する固定容量の予約領域を分ける。
        for (const auto* name : {"shots", "threats"}) {
            // FaultedのVMは列を公開しない。未取得を使用量0と誤解させないよう有効性も返す。
            if (state_.phase == Phase::kFaulted) {
                break;
            }
            const auto view = vm_.InspectPool(name);
            const auto length = view.Column<float>("position", 0).size();
            live += length * ((mode_ == GameMode::k3d ? 9 : 6) * sizeof(float) + sizeof(float) + sizeof(std::uint8_t));
            count += length;
        }
        return {{"vm_owned_bytes", stats.owned_bytes},
                {"vm_peak_owned_bytes", stats.peak_owned_bytes},
                {"vm_module_bytes", memory.module_bytes},
                {"vm_state_bytes", memory.state_bytes},
                {"vm_pool_reserved_bytes", memory.pool_data_bytes},
                {"vm_pool_live_bytes", live},
                {"vm_pool_live_count", count},
                {"vm_pool_live_valid", state_.phase == Phase::kFaulted ? 0u : 1u},
                {"vm_removal_bytes", memory.pool_removal_bytes},
                {"vm_stack_bytes", memory.stack_bytes},
                {"vm_kernel_bytes", memory.kernel_bytes},
                {"vm_management_bytes", memory.management_bytes},
                {"host_dots_reserved_bytes", dots_.capacity() * sizeof(Dot)},
                {"host_actor_reserved_bytes", actors_.capacity() * sizeof(Actor) +
                                                  actor_hits_.capacity() * sizeof(std::array<std::uint32_t, 2>) +
                                                  actor_contacts_.capacity() * sizeof(std::uint32_t)},
                {"actor_count", actors_.size()}};
    }
} // namespace gengine
