#include "game_audio.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
#include <windows.h>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition == false) {
            throw std::runtime_error(message);
        }
    }
    void Pump(gengine::GameAudio& audio, gengine::Phase phase, std::optional<std::uint32_t> track,
              int milliseconds = 80, bool paused = false)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            audio.Update(phase, track, 0, paused);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < end);
    }
} // namespace

int main(int argc, char** argv)
{
    try {
        Require(argc == 6, "requires content, backend, fault DLL, unavailable DLL, output");
        const auto content = std::filesystem::path(argv[1]);
        const auto backends = std::filesystem::path(argv[2]);
        const auto root = std::filesystem::absolute(argv[5]) / std::to_string(GetTickCount64());
        std::filesystem::create_directories(root / "assets");
        std::filesystem::create_directories(root / "audio");
        for (const auto* name : {"shot", "hit", "clear", "fail"}) {
            const auto file = std::string(name) + ".wav";
            std::filesystem::copy_file(content / "assets" / file, root / "assets" / file);
        }
        std::ofstream(root / "audio/game.json")
            << R"({"version":1,"events":["assets/shot.wav","assets/hit.wav","assets/clear.wav","assets/fail.wav","assets/shot.wav"],"tracks":["assets/shot.wav","assets/hit.wav"],"title_track":1,"play_track":2})";
        const auto source = gengine::AssetSource(root);
        const auto defaults = gengine::AssetSource(content);
        auto metrics = gengine::AssetMetrics{};
        const auto scene = gengine::DefaultScene(false);
        {
            auto audio = gengine::GameAudio(defaults, source, scene, backends, true, metrics);
            Require(audio.available(), "real audio device unavailable");
            Pump(audio, gengine::Phase::kStart, {}, 400);
            const auto resident = audio.Stats().pcm_bytes;
            Require(resident > 0 && audio.Stats().voices == 1 && audio.requests() == 1,
                    "title loop missing or multiplied");
            Pump(audio, gengine::Phase::kStart, {}, 300);
            Require(audio.requests() == 1 && audio.MusicInfo()->source_frames > 4800, "loop restarted between frames");

            // 実ミキサーの適用音量・停止位置を読み、設定値だけの照合で済ませない。
            audio.SetSettings({40, 50, 20, false});
            Pump(audio, gengine::Phase::kStart, {});
            Require(std::abs(audio.MusicInfo()->applied_gain - .2f) < .002f, "master/music gain differs");
            audio.SetSettings({40, 50, 20, true});
            Pump(audio, gengine::Phase::kStart, {});
            Require(audio.MusicInfo()->applied_gain == 0, "mute did not silence music");
            audio.SetSettings({40, 50, 20, false});
            Pump(audio, gengine::Phase::kStart, {}, 80, true);
            const auto paused = audio.MusicInfo();
            Pump(audio, gengine::Phase::kStart, {}, 100, true);
            Require(paused && audio.MusicInfo()->state == aengine::PlaybackState::kPaused &&
                        audio.MusicInfo()->source_frames == paused->source_frames,
                    "pause moved the loop");
            audio.TestEffect();
            Pump(audio, gengine::Phase::kStart, {}, 250, true);
            Require(audio.Stats().voices == 1 && audio.MusicInfo()->source_frames == paused->source_frames,
                    "paused audition did not finish independently of music");
            Pump(audio, gengine::Phase::kStart, {});
            Require(audio.MusicInfo()->source_frames > paused->source_frames, "resume did not advance");

            for (auto cycle = 0; cycle < 20; ++cycle) {
                audio.Reset();
                Pump(audio, gengine::Phase::kPlaying, 2);
                Require(audio.Stats().voices == 1 && audio.Stats().playbacks == 1 &&
                            audio.Stats().pcm_bytes == resident,
                        "repeat leaked voices or PCM");
                audio.Update(gengine::Phase::kPlaying, 2, 16, false);
                Pump(audio, gengine::Phase::kPlaying, 2, 250);
                Pump(audio, gengine::Phase::kResult, {});
                Require(audio.Stats().voices == 0 && audio.Stats().playbacks == 0, "result retained music");
                Pump(audio, gengine::Phase::kStart, {});
            }
            Pump(audio, gengine::Phase::kPlaying, 0);
            Require(audio.Stats().voices == 0, "explicit silence ignored");
            auto rejected = false;
            try {
                audio.Update(gengine::Phase::kPlaying, 3, 0, false);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            Require(rejected, "undefined music accepted");
        }

        // 試験用DLLで初期化失敗と再生後の消失を発生させ、通常バックエンドへ差し替える。
        for (const auto input : {argv[3], argv[4]}) {
            const auto isolated = root / (input == argv[3] ? "lost" : "absent");
            std::filesystem::create_directories(isolated);
            const auto destination = isolated / "aengine_backend_xaudio2.dll";
            std::filesystem::copy_file(input, destination);
            auto audio = gengine::GameAudio(defaults, source, scene, isolated, true, metrics);
            audio.SetSettings({40, 50, 20, true});
            Pump(audio, gengine::Phase::kStart, {});
            Require(audio.available() == false && audio.Stats().pcm_bytes == 0, "device failure was not released");
            std::filesystem::copy_file(backends / destination.filename(), destination,
                                       std::filesystem::copy_options::overwrite_existing);
            Pump(audio, gengine::Phase::kStart, {}, 2300, true);
            Require(audio.available() && audio.Stats().voices == 1 && audio.MusicInfo() &&
                        audio.MusicInfo()->state == aengine::PlaybackState::kPaused &&
                        audio.MusicInfo()->applied_gain == 0,
                    "recovery lost pause or mute");
            audio.SetSettings({40, 50, 20, false});
            Pump(audio, gengine::Phase::kStart, {}, 300);
            Require(std::abs(audio.MusicInfo()->applied_gain - .2f) < .002f, "recovery lost gain");
        }
        std::cout << "audio gain/pause/20 cycles/defined events/device recovery passed; " << root << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
