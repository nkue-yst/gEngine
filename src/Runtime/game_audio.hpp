#pragma once
#include "assets.hpp"
#include "audio_settings.hpp"
#include "game.hpp"
#include <aengine/audio.hpp>
#include <chrono>

namespace gengine
{
    // 音源はaEngineだけが常駐PCMを所有する。復旧時は入力元から再登録する。
    class GameAudio final
    {
      public:
        GameAudio(const AssetSource& defaults, const AssetSource& references, const SceneDocument& scene,
                  const std::filesystem::path& backends, bool custom, AssetMetrics& metrics);
        void SetSettings(const AudioSettings& settings);
        const AudioSettings& settings() const;
        void Pause(bool paused);
        void Reset();
        void Stop();
        void TestEffect();
        void StopPreview();
        // 同じ曲は継続し、変更時だけ切り替える。復旧後の曲は先頭から再生する。
        void Update(Phase phase, std::optional<std::uint32_t> track, std::uint32_t events, bool paused);
        bool available() const;
        aengine::Statistics Stats() const;
        std::optional<aengine::PlaybackInfo> MusicInfo() const;
        std::uint64_t requests() const;
        std::uint64_t notifications() const;

      private:
        struct Sound
        {
            std::string path;
            bool reference = false;
            aengine::SoundHandle handle{};
        };
        bool Check(const aengine::Result& result);
        void Lost();
        void Open();
        void PlayEffect(std::uint32_t event, std::uint32_t group = 0);
        void Mix();
        std::unique_ptr<AssetSource> defaults_;
        std::unique_ptr<AssetSource> references_;
        aengine::Settings backend_settings_;
        std::unique_ptr<aengine::AudioContext> context_;
        AssetMetrics& metrics_;
        AudioSettings settings_;
        std::array<Sound, 32> effects_{};
        std::vector<Sound> tracks_;
        std::uint32_t title_track_ = 0;
        std::uint32_t play_track_ = 0;
        std::uint32_t active_track_ = 0;
        aengine::PlaybackHandle music_{};
        bool paused_ = false;
        bool assets_validated_ = false;
        std::chrono::steady_clock::time_point retry_{};
        std::uint64_t requests_ = 0;
        std::uint64_t notifications_ = 0;
    };
} // namespace gengine
