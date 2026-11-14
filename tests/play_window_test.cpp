#include "frame_clock.hpp"
#include "play_window.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <thread>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition == false) {
            throw std::runtime_error(message);
        }
    }
    void Key(HWND window, WPARAM key, bool down)
    {
        SendMessageW(window, down ? WM_KEYDOWN : WM_KEYUP, key, 0);
    }
    void Pump()
    {
        auto message = MSG{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            // 描画が遅くても次のタイマー処理前に自機を観測し、通常キーの回避方向を更新する。
            if (message.message == gengine::FrameClock::kMessage) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    void ResumeNormalInput(gengine::PlayWindow& window)
    {
        // 実デスクトップで別ウィンドウへフォーカスが移った場合だけ、通常のPキーで再開する。
        // 明示的なpauseとfocus-lossの検査中は呼ばない。
        if (window.game().state().paused) {
            SetFocus(window.window());
            Key(window.window(), 'P', true);
            Key(window.window(), 'P', false);
            std::cout << "resumed normal input after desktop focus loss\n";
        }
    }
    void Save(gengine::PlayWindow& window, const std::filesystem::path& file)
    {
        const auto tick = window.game().state().tick;
        const auto png = window.Capture();
        Require(window.game().state().tick == tick, "capture advanced game time");
        auto stream = std::ofstream(file, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        Require(stream.good(), "capture write failed");
    }
} // namespace
int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        Require(argc >= 3 && argc <= 6, "module and output directory required");
        const auto mode = argc >= 4 && std::wstring(argv[3]) == L"3d" ? gengine::GameMode::k3d : gengine::GameMode::k2d;
        auto source = std::optional<gengine::AssetSource>{};
        auto input = gengine::GameInput{};
        if (argc >= 5) {
            source.emplace(argv[4], argc == 5);
            input = gengine::LoadGameInput(*source, mode == gengine::GameMode::k3d);
        }
        else {
            input.module = gengine::ReadModule(argv[1]);
            input.scene = gengine::DefaultScene(mode == gengine::GameMode::k3d);
        }
        const auto& bytes = input.module;
        const auto output = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(output);
        auto executable = std::array<wchar_t, 32768>{};
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        const auto backends = std::filesystem::path(executable.data()).parent_path() / "backends";
        {
            auto window =
                gengine::PlayWindow(nullptr, bytes, backends, mode, input.scene, false, source ? &*source : nullptr);
            const auto assets = window.metrics().assets;
            std::cout << "assets files=" << assets.files_read << " compressed=" << assets.compressed_files
                      << " read_peak=" << assets.read_peak_bytes << " decode_peak=" << assets.decode_peak_bytes
                      << " transfer_peak=" << assets.transfer_peak_bytes
                      << " payload_peak=" << assets.peak_payload_bytes << " resident_pcm=" << assets.resident_pcm_bytes
                      << '\n';
            Save(window, output / "start.png");
            Key(window.window(), VK_RETURN, true);
            Key(window.window(), VK_RETURN, false);
            Key(window.window(), VK_SPACE, true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            bool saved_play = false;
            bool moving_right = true;
            auto frames = std::vector<std::uint64_t>{};
            auto conversions = std::vector<std::uint64_t>{};
            auto renders = std::vector<std::uint64_t>{};
            auto frame_id = std::uint64_t{};
            while (window.game().state().phase == gengine::Phase::kPlaying &&
                   std::chrono::steady_clock::now() < deadline) {
                ResumeNormalInput(window);
                Key(window.window(), VK_SPACE, true);
                // 通常の移動キーで左右に回避しながら射撃する。
                const auto tick = window.game().state().tick;
                const auto x = window.game().state().player.x;
                if (x >= 1.5f) {
                    moving_right = false;
                }
                else if (x <= -1.5f) {
                    moving_right = true;
                }
                Key(window.window(), 'D', moving_right);
                Key(window.window(), 'A', moving_right == false);
                if (tick > 60 && saved_play == false) {
                    Save(window, output / "playing.png");
                    saved_play = true;
                }
                Pump();
                if (window.metrics().frame != frame_id) {
                    frame_id = window.metrics().frame;
                    frames.push_back(window.metrics().frame_ns);
                    conversions.push_back(window.metrics().conversion_ns);
                    renders.push_back(window.metrics().render_ns);
                }
            }
            const auto& result = window.game().state();
            std::cout << "outcome=" << result.outcome << " hp=" << result.hp << " enemy=" << result.enemy_hp
                      << " tick=" << result.tick << '\n';
            Require(result.phase == gengine::Phase::kResult, "window did not reach result");
            Require(result.outcome == 1, "normal input clear route");
            Require(window.metrics().audio_requests > 0 && window.metrics().audio_notifications > 0,
                    "audio playback and completion notifications");
            const auto p95 = [](std::vector<std::uint64_t>& values) {
                std::sort(values.begin(), values.end());
                return values.at(values.size() * 95 / 100);
            };
            std::cout << "normal frame_p95_ns=" << p95(frames) << " conversion_p95_ns=" << p95(conversions)
                      << " render_p95_ns=" << p95(renders)
                      << " host_reserved_bytes=" << window.metrics().host_reserved_bytes
                      << " renderer_reserved_bytes=" << window.metrics().render_reserved_bytes
                      << " audio_pcm_bytes=" << window.metrics().audio_pcm_bytes << '\n';
            Save(window, output / "result.png");
            Key(window.window(), VK_ESCAPE, true);
            Key(window.window(), VK_ESCAPE, false);
            Require(window.game().state().phase == gengine::Phase::kStart, "return to title");
            Require(window.game().Dots().empty(), "reset left bullets");
            Save(window, output / "returned.png");
            // 横長でない窓でもリサイズを通し、同じ論理画面を描ける。
            SetWindowPos(window.window(), nullptr, 0, 0, 800, 800, SWP_NOMOVE | SWP_NOZORDER);
            for (auto i = 0; i < 15; ++i) {
                Pump();
            }
            Require(window.game().state().phase == gengine::Phase::kStart, "resize changed phase");
            Key(window.window(), VK_RETURN, true);
            Key(window.window(), VK_RETURN, false);
            Require(window.game().state().phase == gengine::Phase::kPlaying, "second start");
            if (mode == gengine::GameMode::k3d) {
                // 高さ・奥行きのキー割当てを実ウィンドウ手続きで確認する。
                for (const auto key : {'E', 'Q', 'W', 'S'}) {
                    const auto before = window.game().state().player;
                    const auto before_tick = window.game().state().tick;
                    Key(window.window(), key, true);
                    const auto input_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                    while (window.game().state().tick < before_tick + 6 &&
                           std::chrono::steady_clock::now() < input_deadline) {
                        ResumeNormalInput(window);
                        Key(window.window(), key, true);
                        Pump();
                    }
                    Key(window.window(), key, false);
                    const auto after = window.game().state().player;
                    Require(key == 'E'   ? after.y > before.y
                            : key == 'Q' ? after.y < before.y
                            : key == 'W' ? after.z < before.z
                                         : after.z > before.z,
                            "3D key mapping");
                }
                window.Reset(0);
                Key(window.window(), VK_RETURN, true);
                Key(window.window(), VK_RETURN, false);
            }
            Key(window.window(), 'P', true);
            const auto tick = window.game().state().tick;
            for (auto i = 0; i < 30; ++i) {
                Pump();
            }
            Require(window.game().state().tick == tick, "pause did not freeze simulation");
            Key(window.window(), 'P', false);
            Key(window.window(), 'P', true);
            Key(window.window(), 'P', false);
            SendMessageW(window.window(), WM_KILLFOCUS, 0, 0);
            Require(window.game().state().paused, "focus loss must pause");
            Key(window.window(), 'P', true);
            Key(window.window(), 'P', false);
            const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (window.game().state().phase == gengine::Phase::kPlaying &&
                   std::chrono::steady_clock::now() < failure_deadline) {
                ResumeNormalInput(window);
                Pump();
            }
            std::cout << "failure observation: tick=" << window.game().state().tick
                      << " hp=" << window.game().state().hp << " paused=" << window.game().state().paused
                      << " outcome=" << window.game().state().outcome << '\n';
            Require(window.game().state().outcome == 2, "normal input failure route");
            Save(window, output / "failure.png");
            Key(window.window(), VK_RETURN, true);
            Key(window.window(), VK_RETURN, false);
            Require(window.game().state().phase == gengine::Phase::kPlaying && window.game().state().hp == 3,
                    "retry after failure");
            frames.clear();
            conversions.clear();
            renders.clear();
            auto reserved = gengine::FrameMetrics{};
            for (auto repeat = 0; repeat < 30; ++repeat) {
                window.Reset(4);
                const auto initial_frame = window.metrics().frame;
                while (window.metrics().frame < initial_frame + 4) {
                    Pump();
                    if (window.metrics().frame != frame_id) {
                        frame_id = window.metrics().frame;
                        frames.push_back(window.metrics().frame_ns);
                        conversions.push_back(window.metrics().conversion_ns);
                        renders.push_back(window.metrics().render_ns);
                    }
                }
                if (repeat == 0) {
                    reserved = window.metrics();
                }
                else {
                    Require(window.metrics().host_reserved_bytes == reserved.host_reserved_bytes &&
                                window.metrics().render_reserved_bytes == reserved.render_reserved_bytes &&
                                window.metrics().audio_pcm_bytes == reserved.audio_pcm_bytes,
                            "reset grew host, renderer or PCM reservation");
                }
            }
            std::cout << "load frame_p95_ns=" << p95(frames) << " conversion_p95_ns=" << p95(conversions)
                      << " render_p95_ns=" << p95(renders)
                      << " host_reserved_bytes=" << window.metrics().host_reserved_bytes
                      << " renderer_reserved_bytes=" << window.metrics().render_reserved_bytes
                      << " audio_pcm_bytes=" << window.metrics().audio_pcm_bytes << '\n';
        }
        {
            // 編集表示の反復更新でもHWNDとホスト・描画の予約量を維持する。
            auto scene = gengine::DefaultScene(mode == gengine::GameMode::k3d);
            auto preview = gengine::PlayWindow(nullptr, {}, backends, mode, scene, true);
            const auto view = mode == gengine::GameMode::k2d ? std::optional(gengine::FitScene2d(scene)) : std::nullopt;
            const auto handle = preview.window();
            preview.UpdatePreviewScene(scene, view, "player");
            preview.Capture();
            preview.UpdatePreviewScene(scene, view, "player");
            const auto reservation = preview.metrics();
            for (auto repeat = 0; repeat < 64; ++repeat) {
                scene.objects[0].position[0] = float(repeat % 3);
                preview.UpdatePreviewScene(scene, view, "player");
                Require(preview.window() == handle, "preview recreated HWND");
                Require(preview.metrics().host_reserved_bytes == reservation.host_reserved_bytes &&
                            preview.metrics().render_reserved_bytes == reservation.render_reserved_bytes &&
                            preview.metrics().audio_pcm_bytes == 0,
                        "preview update grew reservations or initialized audio");
            }
            std::cout << "preview 64 updates: stable HWND, host and renderer reservation; no audio\n";
        }
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        CoUninitialize();
        return 1;
    }
}
