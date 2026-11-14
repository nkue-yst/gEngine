#include "play_window.hpp"
#include "png.hpp"
#include <BinPackLib/PackFiles.h>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <psapi.h>

namespace
{
    void Require(bool value, const char* reason)
    {
        if (value == false) {
            throw std::runtime_error(reason);
        }
    }
    void Write(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        auto file = std::ofstream(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(file.good(), "cannot write fixture");
    }
    void Text(const std::filesystem::path& path, const std::string& text)
    {
        Write(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }
    void Put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        for (auto i = 0u; i < 4; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
        }
    }
    template <class F> void Reject(F action, std::string_view reason)
    {
        try {
            action();
        } catch (const std::exception& error) {
            Require(std::string_view(error.what()).find(reason) != std::string_view::npos, error.what());
            return;
        }
        throw std::runtime_error("expected failure: " + std::string(reason));
    }
    std::uint64_t PrivateBytes()
    {
        auto memory = PROCESS_MEMORY_COUNTERS_EX{};
        Require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                     sizeof(memory)) != FALSE,
                "memory query failed");
        return memory.PrivateUsage;
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        Require(argc == 3, "content and output required");
        const auto content = std::filesystem::path(argv[1]);
        const auto output = std::filesystem::path(argv[2]);
        const auto root = output / L"content";
        const auto backends = std::filesystem::absolute(argv[0]).parent_path() / "backends";
        std::filesystem::create_directories(root);
        std::filesystem::copy(content, root,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        const auto native = gengine::AssetSource(root);
        auto metrics = gengine::AssetMetrics{};
        const auto module = native.Read("shooter2d.glbc", 64 * 1024 * 1024, metrics);
        Write(root / L"日本語 モジュール.glbc", module);
        Require(gengine::ReadModule(root / L"日本語 モジュール.glbc") == module, "UTF-8 module path failed");

        // 標準素材と違う画像・形状・PCM長を作り、参照が実際に使われたことを測定する。
        auto pixels = std::vector<std::uint8_t>(16 * 16 * 4, 255);
        for (auto i = std::size_t{0}; i < pixels.size(); i += 4) {
            pixels[i] = 40;
            pixels[i + 1] = 90;
        }
        const auto image = gengine::EncodePng(pixels, 16, 16);
        Write(root / L"custom/日本語 画像.png", image);
        auto mesh = native.Read("assets/sphere.gmesh", 4 * 1024 * 1024, metrics);
        // 最初の頂点だけでなく、全頂点のX成分を伸ばして球とは異なる輪郭にする。
        const auto count = gengine::DecodeMesh(mesh).vertices.size();
        for (auto i = std::size_t{0}; i < count; ++i) {
            const auto offset = 16 + i * 32;
            const auto bits = std::uint32_t(mesh[offset]) | (std::uint32_t(mesh[offset + 1]) << 8) |
                              (std::uint32_t(mesh[offset + 2]) << 16) | (std::uint32_t(mesh[offset + 3]) << 24);
            Put(mesh, offset, std::bit_cast<std::uint32_t>(std::bit_cast<float>(bits) * 2.0f));
        }
        Write(root / L"custom/日本語 形状.gmesh", mesh);
        auto wav = native.Read("assets/shot.wav", 1024 * 1024, metrics);
        wav.resize(44 + 2400);
        Put(wav, 4, static_cast<std::uint32_t>(wav.size() - 8));
        Put(wav, 40, 2400);
        Write(root / L"custom/短い音.wav", wav);
        for (const auto three : {false, true}) {
            auto scene = gengine::DefaultScene(three);
            scene.objects[0].visual = three ? "custom/日本語 形状.gmesh" : "custom/日本語 画像.png";
            scene.objects[1].visual = scene.objects[0].visual;
            scene.objects[0].audio = "custom/短い音.wav";
            scene.objects[1].audio = "custom/短い音.wav";
            Text(root / "scenes" / (three ? "shooter3d.json" : "shooter2d.json"), gengine::SerializeScene(scene));
        }
        auto paths = std::vector<std::string>{};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) {
                paths.push_back(entry.path().string());
            }
        }
        auto options = bin_pack_lib::PackOptions{};
        options.base_directory = root.string();
        options.overwrite = true;
        options.enable_compression = true;
        const auto pack_path = output / "references.bpack";
        Require(bin_pack_lib::PackFiles(paths, pack_path.string(), options).success, "reference pack creation failed");
        const auto packed = gengine::AssetSource(pack_path, true);

        for (const auto three : {false, true}) {
            const auto mode = three ? gengine::GameMode::k3d : gengine::GameMode::k2d;
            const auto a = gengine::LoadGameInput(native, three);
            const auto b = gengine::LoadGameInput(packed, three);
            Require(a.scene == b.scene && a.module == b.module, "referenced input differs");
            for (const auto& reference : gengine::InspectSceneAssets(a.scene, packed)) {
                Require(reference.diagnostic.empty(), "packed reference inspection failed");
            }
            auto captures = std::vector<std::vector<std::uint8_t>>{};
            for (const auto* source : {&native, &packed}) {
                auto window = gengine::PlayWindow(nullptr, a.module, backends, mode, a.scene, false, source);
                captures.push_back(window.Capture());
                Require(window.metrics().audio_pcm_bytes == 14400, "assigned WAV did not replace default PCM");
            }
            Require(captures[0] == captures[1], "assigned native/pack GPU image differs");
            Write(output / (three ? "assigned-3d.png" : "assigned-2d.png"), captures[0]);
            {
                auto standard = gengine::PlayWindow(nullptr, a.module, backends, mode, gengine::DefaultScene(three),
                                                    false, &native);
                Require(standard.Capture() != captures[0], "assigned visual has no effect");
            }

            auto game_a = gengine::Game(a.module, 1000000, mode, a.scene);
            auto game_b = gengine::Game(b.module, 1000000, mode, b.scene);
            game_a.Begin();
            game_b.Begin();
            for (auto tick = 0; tick < 300; ++tick) {
                game_a.Step({false, false, false, false, true});
                game_b.Step({false, false, false, false, true});
                const auto& x = game_a.state();
                const auto& y = game_b.state();
                Require(x.phase == y.phase && x.hp == y.hp && x.enemy_hp == y.enemy_hp && x.outcome == y.outcome &&
                            game_a.TakeSounds() == game_b.TakeSounds(),
                        "referenced game trajectory differs");
            }
            Require(game_a.state().phase == gengine::Phase::kResult, "referenced normal game did not finish");
            game_a.Reset();
            game_a.Begin();
            Require(game_a.state().phase == gengine::Phase::kPlaying && game_a.Dots().empty(),
                    "referenced restart failed");

            // 同じ素材状態で生存資源とGPU容量を照合し、非同期回収台帳は固定予算で検査する。
            // CPU予約量にはGPU完了時刻で増える台帳の最大使用量が含まれ、周回間の完全一致は保証されない。
            auto preview = gengine::PlayWindow(nullptr, {}, backends, mode, a.scene, true, &native);
            const auto handle = preview.window();
            auto baseline = std::array<gengine::FrameMetrics, 2>{};
            auto private_before = std::uint64_t{0};

            // この小さなプレビューの最大8資源と回収台帳を合わせ、CPU管理量を8KiB以内に保つ。
            constexpr auto kPreviewCpuBudget = std::uint64_t{8 * 1024};
            for (auto repeat = 0; repeat < 32; ++repeat) {
                auto scene = repeat % 2 == 0 ? gengine::DefaultScene(three) : a.scene;
                preview.UpdatePreviewAssets(scene, native);
                preview.UpdatePreviewScene(scene);
                preview.Capture();

                // 読み戻しの完了後に通常描画の統計を更新し、古い転送・一時Targetの解放を確認する。
                preview.UpdatePreviewScene(scene);
                Require(preview.window() == handle, "reference change recreated preview HWND");
                std::cout << (three ? "3d" : "2d") << " reference cycle=" << repeat
                          << " renderer_reserved=" << preview.metrics().render_reserved_bytes
                          << " deferred=" << preview.metrics().render_deferred_bytes
                          << " live=" << preview.metrics().render_live_resources << std::endl;
                const auto& current = preview.metrics();
                Require(current.render_reserved_bytes <= kPreviewCpuBudget, "preview CPU reservation exceeded budget");
                Require(current.render_deferred_bytes == 0, "completed reference resources were not collected");
                if (repeat == 14 || repeat == 15) {
                    baseline[repeat % 2] = current;
                    private_before = PrivateBytes();
                }
                if (repeat >= 16) {
                    const auto& expected = baseline[repeat % 2];
                    Require(current.render_live_resources == expected.render_live_resources &&
                                current.render_gpu_bytes == expected.render_gpu_bytes &&
                                current.host_reserved_bytes == expected.host_reserved_bytes &&
                                current.render_capacity_growths == expected.render_capacity_growths,
                            "reference changes retained resources or grew frame storage");
                }
            }
            Require(PrivateBytes() <= private_before + 16 * 1024 * 1024, "reference changes grew process memory");
            const auto reads = preview.metrics().assets.files_read;
            preview.UpdatePreviewAssets(a.scene, native);
            Require(preview.metrics().assets.files_read == reads, "unchanged reference was decoded again");
            std::cout << (three ? "3d" : "2d")
                      << " assigned PCM=14400 renderer_reserved=" << preview.metrics().render_reserved_bytes
                      << " private_before=" << private_before << " private_after=" << PrivateBytes() << '\n';

            auto broken = a.scene;
            broken.objects[0].visual = "custom/missing.png";
            const auto references = gengine::InspectSceneAssets(broken, native);
            Require(references.front().diagnostic.empty() == false, "broken reference lacks diagnosis");
            preview.UpdatePreviewAssets(broken, native);
            preview.UpdatePreviewScene(broken);
            Require(preview.Capture().empty() == false, "broken reference prevented editing");
            Reject(
                [&] {
                    gengine::PlayWindow window(nullptr, a.module, backends, mode, broken, false, &native);
                },
                "player.visual");
            broken = a.scene;
            broken.objects[1].audio = "custom/missing.wav";
            Reject(
                [&] {
                    gengine::PlayWindow window(nullptr, a.module, backends, mode, broken, false, &native);
                },
                "missing.wav");
        }
        std::cout << "assigned GPU/PCM, native/pack parity, normal loop, diagnostics and resource reuse passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
