#include <aengine/audio.hpp>
#include <rengine/renderer.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    template <class T> void Require(const T& result, const char* message)
    {
        if (static_cast<bool>(result) == false) {
            throw std::runtime_error(message);
        }
    }

    void CheckComponents(const std::filesystem::path& backend_directory)
    {
        auto render_settings = rengine::Settings();
        render_settings.backend = "d3d12";
        render_settings.backend_directory = backend_directory;
        const auto renderer = rengine::Renderer::Create(render_settings);
        if (renderer.has_value() == false) {
            throw std::runtime_error("rEngine initialization: " + renderer.error().message);
        }

        auto audio_settings = aengine::Settings();
        audio_settings.backend_directory = backend_directory;
        audio_settings.voice_capacity = {0, 0, 1, 0};
        const auto audio = aengine::AudioContext::Create(audio_settings);
        if (audio.has_value() == false) {
            throw std::runtime_error(
                "aEngine initialization: code=" + std::to_string(static_cast<int>(audio.error().code)) +
                ", detail=" + std::to_string(audio.error().detail));
        }

        // 無音PCMを送出し、音声ワーカーが動いている間に同じホストからGPUを操作する。
        const auto pcm = std::array<std::byte, 960>{};
        const auto sound = (*audio)->CreatePcm({48000, 1, pcm});
        Require(sound, "CreatePcm failed");
        const auto playback = (*audio)->Play(*sound);
        Require(playback, "Play failed");

        const auto target = (*renderer)->CreateTarget({4, 4});
        Require(target, "CreateTarget failed");
        auto pass = rengine::DefaultPass();
        pass.target = *target;
        pass.clear_color[1] = 1;
        Require((*renderer)->Render({&pass, 1, 0}), "Render failed");

        auto pixels = std::array<std::uint8_t, 64>{};
        Require((*renderer)->ReadTarget(*target, pixels), "ReadTarget failed");
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            Require(pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0 && pixels[i + 3] == 255,
                    "Render readback did not match green RGBA pixels");
        }

        // 非同期の完了通知を期限付きで待ち、再生失敗や停止を自然終了と区別する。
        auto events = std::array<aengine::PlaybackEvent, 1>{};
        bool completed = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            Require((*audio)->Update(), "Audio update failed");
            const auto count = (*audio)->DrainEvents(events);
            Require(count, "DrainEvents failed");
            if (*count != 0) {
                Require(events[0].playback == *playback && events[0].reason == aengine::EndReason::kCompleted &&
                            events[0].source_frames == pcm.size() / 2,
                        "Audio playback did not complete normally");
                completed = true;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Require(completed, "Audio completion timed out");

        // 同じ所有スレッドで資源を解放し、ワーカーとGPUの完了を待って終了する。
        Require((*audio)->ReleaseSound(*sound), "ReleaseSound failed");
        Require((*audio)->Update(), "Audio resource collection failed");
        Require((*audio)->Shutdown(), "Audio shutdown failed");
        Require((*renderer)->DestroyTarget(*target), "DestroyTarget failed");
        Require((*renderer)->WaitIdle(), "WaitIdle failed");
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    try {
        Require(argc == 1, "No arguments are accepted");
        const auto executable_directory = std::filesystem::absolute(argv[0]).parent_path();
        Require(std::filesystem::is_regular_file(executable_directory / "ThirdParty/rEngine-LICENSE.txt"),
                "rEngine license was not staged");

        // 破棄後にも両部品を再作成できることを、同じ公開APIの経路で確認する。
        for (int run = 0; run < 2; ++run) {
            CheckComponents(executable_directory / "backends");
        }

        std::cout << "aEngine + rEngine: playback, render readback and recreation "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
