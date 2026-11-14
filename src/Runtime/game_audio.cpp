#include "game_audio.hpp"
#include <algorithm>
#include <stdexcept>

namespace gengine
{
    namespace
    {
        bool DeviceFailure(aengine::ErrorCode code)
        {
            return code == aengine::ErrorCode::kDeviceLost || code == aengine::ErrorCode::kBackendFailure ||
                   code == aengine::ErrorCode::kBackendUnavailable;
        }
    } // namespace

    GameAudio::GameAudio(const AssetSource& defaults, const AssetSource& references, const SceneDocument& scene,
                         const std::filesystem::path& backends, bool custom, AssetMetrics& metrics)
        : defaults_(defaults.Reopen()), references_(references.Reopen()), metrics_(metrics)
    {
        backend_settings_.backend_directory = backends;
        const auto names = std::array{"shot", "hit", "clear", "fail"};
        for (auto event = 0u; event < names.size(); ++event) {
            effects_[event].path = std::string("assets/") + names[event] + ".wav";
        }
        if (custom) {
            const auto manifest = DecodeAudioManifest(references.Read("audio/game.json", 16384, metrics));
            for (auto index = 0u; index < manifest.events.size(); ++index) {
                effects_[index] = {manifest.events[index], true, {}};
            }
            for (const auto& path : manifest.tracks) {
                tracks_.push_back({path, true, {}});
            }
            title_track_ = manifest.title_track;
            play_track_ = manifest.play_track;
        }
        // シーンで明示した射撃・被弾の割当てを優先し、PIEとRunnerで同じ音を使う。
        for (auto index = 0u; index < 2; ++index) {
            const auto kind = index == 0 ? "player_spawn" : "enemy_spawn";
            const auto object = std::find_if(scene.objects.begin(), scene.objects.end(), [&](const auto& value) {
                return value.kind == kind;
            });
            if (object != scene.objects.end() && object->audio) {
                effects_[index] = {*object->audio, true, {}};
            }
        }
        Open();
    }

    void GameAudio::Open()
    {
        auto candidate = aengine::AudioContext::Create(backend_settings_);
        if (candidate.has_value() == false && DeviceFailure(candidate.error().code) == false) {
            throw std::runtime_error("cannot initialize audio backend");
        }
        // 初回に検査した入力を、デバイス不在の待機中に繰り返し展開しない。
        if (candidate.has_value() == false && assets_validated_) {
            Lost();
            return;
        }
        auto pcm_bytes = std::uint64_t{0};
        const auto load = [&](Sound& sound, std::uint64_t limit) {
            sound.handle = {};
            if (sound.path.empty()) {
                return;
            }
            if (AssetKind(sound.path) != "audio") {
                throw std::invalid_argument(sound.path + ": expected WAV asset");
            }
            const auto wav = (sound.reference ? references_ : defaults_)->Read(sound.path, limit, metrics_);
            const auto pcm = aengine::InspectWav(std::as_bytes(std::span(wav)));
            if (pcm.has_value() == false) {
                throw std::invalid_argument(sound.path + ": invalid PCM16 WAV");
            }
            pcm_bytes += pcm->bytes.size();
            if (pcm_bytes > backend_settings_.pcm_budget) {
                throw std::invalid_argument("game audio exceeds 64 MiB PCM budget");
            }
            if (candidate) {
                const auto registered = (*candidate)->CreateWav(std::as_bytes(std::span(wav)));
                if (registered.has_value() == false) {
                    throw std::runtime_error(sound.path + ": cannot register PCM");
                }
                sound.handle = *registered;
            }
            metrics_.transfer_peak_bytes = std::max(metrics_.transfer_peak_bytes, wav.capacity() + pcm_bytes);
            metrics_.peak_payload_bytes = std::max(metrics_.peak_payload_bytes, metrics_.transfer_peak_bytes);
        };
        // デバイス不在でも素材の破損・容量超過を検査する。PCMの二重常駐は行わない。
        for (auto& sound : effects_) {
            load(sound, 1024 * 1024);
        }
        for (auto& sound : tracks_) {
            load(sound, 16 * 1024 * 1024);
        }
        assets_validated_ = true;
        if (candidate) {
            context_ = std::move(*candidate);
            metrics_.resident_pcm_bytes = pcm_bytes;
            active_track_ = 0;
            music_ = {};
            Mix();
            Pause(paused_);
        }
        else {
            Lost();
        }
    }

    void GameAudio::Lost()
    {
        context_.reset();
        active_track_ = 0;
        music_ = {};
        metrics_.resident_pcm_bytes = 0;
        retry_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
    bool GameAudio::Check(const aengine::Result& result)
    {
        if (result) {
            return true;
        }
        const auto stats = context_ ? context_->Stats() : aengine::Expected<aengine::Statistics>{};
        if (DeviceFailure(result.error().code) || (stats && stats->context != aengine::ContextState::kRunning)) {
            Lost();
            return false;
        }
        throw std::runtime_error("audio command failed");
    }
    void GameAudio::Mix()
    {
        if (context_ && Check(context_->SetMasterVolume(float(settings_.master) / 100, settings_.muted)) &&
            Check(context_->SetGroupVolume(0, float(settings_.effects) / 100)) &&
            Check(context_->SetGroupVolume(2, float(settings_.effects) / 100))) {
            Check(context_->SetGroupVolume(1, float(settings_.music) / 100));
        }
    }
    void GameAudio::SetSettings(const AudioSettings& settings)
    {
        if (ValidAudioSettings(settings) == false) {
            throw std::invalid_argument("invalid audio settings");
        }
        settings_ = settings;
        Mix();
    }
    const AudioSettings& GameAudio::settings() const
    {
        return settings_;
    }

    void GameAudio::Pause(bool paused)
    {
        paused_ = paused;
        if (context_ && Check(context_->SetGroupPaused(0, paused))) {
            Check(context_->SetGroupPaused(1, paused));
        }
    }
    void GameAudio::Stop()
    {
        // 障害処理からも呼ぶため、停止要求すら受け付けない場合はコンテキストごと回収する。
        if (context_ && (context_->StopGroup(0).has_value() == false || context_->StopGroup(1).has_value() == false ||
                         context_->StopGroup(2).has_value() == false)) {
            Lost();
        }
        active_track_ = 0;
        music_ = {};
    }
    void GameAudio::Reset()
    {
        Stop();
        Pause(false);
    }
    void GameAudio::PlayEffect(std::uint32_t event, std::uint32_t group)
    {
        if (context_ == nullptr || effects_[event].path.empty()) {
            return;
        }
        auto info = aengine::PlayInfo{};
        info.group = group;
        const auto result = context_->Play(effects_[event].handle, info);
        if (result) {
            ++requests_;
        }
        else if (result.error().code != aengine::ErrorCode::kCapacityExceeded &&
                 result.error().code != aengine::ErrorCode::kQueueFull) {
            Check(std::unexpected(result.error()));
        }
    }
    void GameAudio::TestEffect()
    {
        // ポーズ中でも設定の試聴だけは再生できる独立グループを使う。
        PlayEffect(0, 2);
    }
    void GameAudio::StopPreview()
    {
        if (context_) {
            Check(context_->StopGroup(2));
        }
    }

    void GameAudio::Update(Phase phase, std::optional<std::uint32_t> track, std::uint32_t events, bool paused)
    {
        const auto desired = phase == Phase::kStart     ? title_track_
                             : phase == Phase::kPlaying ? track.value_or(play_track_)
                                                        : 0u;
        if (desired > tracks_.size()) {
            throw std::runtime_error("game requested an undefined music track");
        }
        if (paused_ != paused) {
            Pause(paused);
        }
        if (context_ == nullptr && std::chrono::steady_clock::now() >= retry_) {
            Open();
        }
        if (context_ == nullptr) {
            return;
        }
        if (Stats().context != aengine::ContextState::kRunning) {
            Lost();
            return;
        }
        if (phase == Phase::kFaulted) {
            Stop();
            return;
        }
        // 一曲につき再生枠は一つ。停止を先に要求し、終了通知は毎フレーム回収する。
        if (desired != active_track_) {
            if (Check(context_->StopGroup(1)) == false) {
                return;
            }
            active_track_ = 0;
            music_ = {};
        }
        if (Check(context_->Update()) == false) {
            return;
        }
        auto finished = std::array<aengine::PlaybackEvent, 64>{};
        const auto count = context_->DrainEvents(finished);
        if (count.has_value() == false) {
            Check(std::unexpected(count.error()));
            return;
        }
        notifications_ += *count;
        for (auto index = std::size_t{0}; index < *count; ++index) {
            if (finished[index].playback == music_) {
                active_track_ = 0;
                music_ = {};
            }
        }
        if (active_track_ == 0 && desired != 0) {
            auto info = aengine::PlayInfo{};
            info.group = 1;
            info.loop = true;
            info.protected_voice = true;
            info.volume = 0;
            const auto started = context_->Play(tracks_[desired - 1].handle, info);
            if (started.has_value() == false) {
                Check(std::unexpected(started.error()));
                return;
            }
            music_ = *started;
            active_track_ = desired;
            ++requests_;
            if (Check(context_->FadeVolume(music_, 1, std::chrono::milliseconds(250))) == false) {
                return;
            }
        }
        for (auto index = 0u; index < effects_.size(); ++index) {
            if ((events & (1u << index)) != 0) {
                PlayEffect(index);
            }
        }
    }
    bool GameAudio::available() const
    {
        return context_ != nullptr;
    }
    aengine::Statistics GameAudio::Stats() const
    {
        if (context_) {
            const auto stats = context_->Stats();
            if (stats) {
                return *stats;
            }
        }
        auto result = aengine::Statistics{};
        result.context = aengine::ContextState::kUnavailable;
        return result;
    }
    std::uint64_t GameAudio::requests() const
    {
        return requests_;
    }
    std::uint64_t GameAudio::notifications() const
    {
        return notifications_;
    }
    std::optional<aengine::PlaybackInfo> GameAudio::MusicInfo() const
    {
        if (context_ && music_.owner != 0) {
            const auto value = context_->GetPlaybackInfo(music_);
            if (value) {
                return *value;
            }
        }
        return {};
    }
} // namespace gengine
