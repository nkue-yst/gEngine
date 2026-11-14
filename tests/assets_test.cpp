#include "assets.hpp"
#include "play_window.hpp"
#include "png.hpp"
#include <BinPackLib/PackFiles.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <psapi.h>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition == false) {
            throw std::runtime_error(message);
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
        throw std::runtime_error("expected rejection: " + std::string(reason));
    }

    void Write(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        auto stream = std::ofstream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(stream.good(), "fixture write failed");
    }

    std::uint64_t PrivateBytes(bool peak = false)
    {
        auto memory = PROCESS_MEMORY_COUNTERS_EX{};
        Require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                     sizeof(memory)) != FALSE,
                "process memory query failed");
        return peak ? memory.PeakPagefileUsage : memory.PrivateUsage;
    }

    DWORD Handles()
    {
        auto count = DWORD{0};
        Require(GetProcessHandleCount(GetCurrentProcess(), &count) != FALSE, "handle query failed");
        return count;
    }

    int PlayWindows()
    {
        auto count = 0;
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM context) -> BOOL {
                auto name = std::array<wchar_t, 64>{};
                GetClassNameW(window, name.data(), static_cast<int>(name.size()));
                if (std::wstring_view(name.data()) == L"gEnginePlayView") {
                    ++*reinterpret_cast<int*>(context);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&count));
        return count;
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        Require(argc == 4, "content, compressed pack and test output required");
        const auto root = std::filesystem::absolute(argv[3]);
        std::filesystem::create_directories(root);
        const auto content = std::filesystem::path(argv[1]);
        const auto backends = std::filesystem::absolute(argv[0]).parent_path() / "backends";
        auto native = gengine::AssetSource(content);
        auto packed = gengine::AssetSource(argv[2], true);
        auto metrics = gengine::AssetMetrics{};
        auto options = bin_pack_lib::PackOptions{};
        options.base_directory = content.string();
        options.overwrite = true;
        auto paths = std::vector<std::string>{};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(content)) {
            if (entry.is_regular_file()) {
                paths.push_back(entry.path().string());
            }
        }
        const auto raw_pack = root / "uncompressed.bpack";
        Require(bin_pack_lib::PackFiles(paths, raw_pack.string(), options).success, "uncompressed pack failed");
        auto raw = gengine::AssetSource(raw_pack, true);

        // 圧縮有無を含め、全入力とデコード後の画像・頂点を同じバイト列として比較する。
        for (const auto& entry : paths) {
            const auto name = std::filesystem::relative(entry, content).generic_string();
            const auto expected = native.Read(name, 64 * 1024 * 1024, metrics);
            Require(packed.Read(name, 64 * 1024 * 1024, metrics) == expected, "compressed bytes differ");
            Require(raw.Read(name, 64 * 1024 * 1024, metrics) == expected, "uncompressed bytes differ");
        }
        Require(metrics.compressed_files > 0, "compression was not exercised");
        const auto png = native.Read("assets/sprite.png", 1024 * 1024, metrics);
        const auto image = gengine::DecodePng(png);
        const auto packed_image = gengine::DecodePng(packed.Read("assets/sprite.png", 1024 * 1024, metrics));
        Require(image.width == 16 && image.height == 16 && image.pixels == packed_image.pixels, "decoded PNG differs");
        // 細長いatlasは許可し、軸・画素数のどちらの上限も展開前に検査する。
        for (const auto size : {std::array{4096u, 1u}, std::array{4097u, 1u}, std::array{2049u, 2048u}}) {
            auto pixels = std::vector<std::uint8_t>(std::size_t(size[0]) * size[1] * 4, 128);
            const auto encoded = gengine::EncodePng(pixels, size[0], size[1]);
            if (size[0] == 4096) {
                Require(gengine::DecodePng(encoded).width == 4096, "wide atlas was rejected");
            }
            else {
                Reject(
                    [&] {
                        gengine::DecodePng(encoded);
                    },
                    "dimensions");
            }
        }
        const auto mesh_bytes = native.Read("assets/sphere.gmesh", 1024 * 1024, metrics);
        const auto mesh = gengine::DecodeMesh(mesh_bytes);
        const auto packed_mesh = gengine::DecodeMesh(packed.Read("assets/sphere.gmesh", 1024 * 1024, metrics));
        Require(mesh.vertices.size() == 117 && mesh.indices == packed_mesh.indices &&
                    std::memcmp(mesh.vertices.data(), packed_mesh.vertices.data(),
                                mesh.vertices.size() * sizeof(rengine::Vertex)) == 0,
                "decoded mesh differs");

        for (const auto is_3d : {false, true}) {
            const auto mode = is_3d ? gengine::GameMode::k3d : gengine::GameMode::k2d;
            const auto a = gengine::LoadGameInput(native, is_3d);
            const auto b = gengine::LoadGameInput(packed, is_3d);
            Require(a.module == b.module && a.scene == b.scene, "game inputs differ");
            auto game_a = gengine::Game(a.module, 1000000, mode, a.scene);
            auto game_b = gengine::Game(b.module, 1000000, mode, b.scene);
            game_a.Begin();
            game_b.Begin();
            for (auto tick = 0; tick < 300; ++tick) {
                game_a.Step({false, false, false, false, true});
                game_b.Step({false, false, false, false, true});
                const auto& x = game_a.state();
                const auto& y = game_b.state();
                Require(x.phase == y.phase && x.tick == y.tick && x.hp == y.hp && x.enemy_hp == y.enemy_hp &&
                            x.outcome == y.outcome && game_a.TakeSounds() == game_b.TakeSounds(),
                        "game trajectories differ");
            }
            Require(game_a.state().phase == gengine::Phase::kResult, "normal game did not finish");
            game_a.Reset();
            game_b.Reset();
            Require(game_a.Dots().empty() && game_b.Dots().empty(), "return left objects");
            game_a.Begin();
            game_b.Begin();
            Require(game_a.state().phase == game_b.state().phase, "restart differs");

            // 実際のGPU描画も比較し、PCMが登録されていることと入力の解放後の動作を確認する。
            auto captures = std::vector<std::vector<std::uint8_t>>{};
            for (const auto* source : {&native, &packed, &raw}) {
                auto window = gengine::PlayWindow(nullptr, a.module, backends, mode, a.scene, false, source);
                captures.push_back(window.Capture());
                Require(window.metrics().audio_pcm_bytes == 19200, "WAV PCM resident size");
                Require(window.metrics().assets.files_read == 5, "unused assets loaded");
                const auto& m = window.metrics().assets;
                std::cout << (is_3d ? "3d" : "2d") << " compressed=" << m.compressed_files
                          << " read=" << m.read_peak_bytes << " decode=" << m.decode_peak_bytes
                          << " transfer=" << m.transfer_peak_bytes << " payload=" << m.peak_payload_bytes
                          << " PCM=" << m.resident_pcm_bytes << '\n';
            }
            Require(captures[0] == captures[1] && captures[0] == captures[2], "native/pack GPU image differs");
            Write(root / (is_3d ? "3d.png" : "2d.png"), captures[0]);
        }

        // 読込み・デコードの各境界で欠落、過大入力、形式違い、破損を拒否する。
        for (const auto* source : {&native, &packed}) {
            Reject(
                [&] {
                    source->Read("missing.wav", 1024, metrics);
                },
                "missing");
            Reject(
                [&] {
                    source->Read("assets/sprite.png", 1, metrics);
                },
                "limit");
            for (const auto* path :
                 {"../escape", "/absolute", "assets//sprite.png", "assets/../sprite.png", "C:/x", "assets/x."}) {
                Reject(
                    [&] {
                        source->Read(path, 1024, metrics);
                    },
                    "path");
            }
        }
        Reject(
            [&] {
                gengine::DecodePng(mesh_bytes);
            },
            "PNG");
        Reject(
            [&] {
                gengine::DecodePng(std::span(png).first(24));
            },
            "PNG");
        Reject(
            [&] {
                gengine::DecodeMesh(std::span(mesh_bytes).first(20));
            },
            "length");
        auto bad_mesh = mesh_bytes;
        std::fill(bad_mesh.end() - 4, bad_mesh.end(), 255);
        Reject(
            [&] {
                gengine::DecodeMesh(bad_mesh);
            },
            "index");
        bad_mesh = mesh_bytes;
        bad_mesh[18] = 0xc0;
        bad_mesh[19] = 0x7f;
        Reject(
            [&] {
                gengine::DecodeMesh(bad_mesh);
            },
            "coordinate");
        const auto bad_pack = root / "corrupt.bpack";
        Write(bad_pack, png);
        Reject(
            [&] {
                gengine::AssetSource source(bad_pack, true);
            },
            "pack");
        options.base_directory = content.string();
        Require(bin_pack_lib::PackFiles({(content / "assets/sprite.png").string()}, bad_pack.string(), options).success,
                "CRC fixture pack failed");
        {
            auto file = std::fstream(bad_pack, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(-1, std::ios::end);
            file.put(0);
        }
        Reject(
            [&] {
                gengine::AssetSource(bad_pack, true).Read("assets/sprite.png", 1024 * 1024, metrics);
            },
            "CRC32");
        // 圧縮payloadの破損と、展開前のサイズ制限を別々に確認する。
        options.enable_compression = true;
        const auto make_compressed = [&] {
            Require(bin_pack_lib::PackFiles({(content / "assets/sphere.gmesh").string()}, bad_pack.string(), options)
                        .success,
                    "compressed corruption fixture failed");
        };
        make_compressed();
        {
            auto file = std::fstream(bad_pack, std::ios::binary | std::ios::in | std::ios::out);
            // 形式1のヘッダーにあるdata_offsetから、圧縮データの先頭を破損させる。
            auto offset = std::uint64_t{0};
            file.seekg(56);
            file.read(reinterpret_cast<char*>(&offset), sizeof(offset));
            file.seekp(static_cast<std::streamoff>(offset));
            const auto invalid = std::array<std::uint8_t, 16>{};
            file.write(reinterpret_cast<const char*>(invalid.data()), invalid.size());
            Require(file.good(), "compressed fixture write");
        }
        Reject(
            [&] {
                gengine::AssetSource(bad_pack, true).Read("assets/sphere.gmesh", 4 * 1024 * 1024, metrics);
            },
            "assets/sphere.gmesh");
        make_compressed();
        {
            auto file = std::fstream(bad_pack, std::ios::binary | std::ios::in | std::ios::out);
            // ヘッダー80 B + エントリ内24 Bのoriginal_sizeだけを過大値にする。
            const auto oversized = std::uint64_t{128 * 1024 * 1024};
            file.seekp(104);
            file.write(reinterpret_cast<const char*>(&oversized), sizeof(oversized));
            Require(file.good(), "oversized fixture write");
        }
        Reject(
            [&] {
                gengine::AssetSource(bad_pack, true).Read("assets/sphere.gmesh", 4 * 1024 * 1024, metrics);
            },
            "limit");
        auto module = gengine::LoadGameInput(native, false).module;
        module[4] = 255;
        Reject(
            [&] {
                gengine::Game game(module);
            },
            "UnsupportedVersion");

        // 最後の音声で失敗させ、それ以前に作ったGPU・PCM・HWNDも破棄されることを反復確認する。
        const auto broken = root / L"日本語 空白";
        std::filesystem::create_directories(broken / "assets");
        for (const auto& entry : std::filesystem::directory_iterator(content / "assets")) {
            std::filesystem::copy_file(entry.path(), broken / "assets" / entry.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing);
        }
        auto broken_source = gengine::AssetSource(broken);
        Require(broken_source.Read("assets/sprite.png", 1024 * 1024, metrics) == png, "Japanese root failed");
        Write(broken / "assets/fail.wav", png);
        std::filesystem::create_directories(broken / "scenes");
        Write(broken / "scenes/shooter2d.json", png);
        Reject(
            [&] {
                gengine::LoadGameInput(broken_source, false);
            },
            "scenes/shooter2d.json");
        std::filesystem::copy_file(content / "scenes/shooter3d.json", broken / "scenes/shooter2d.json",
                                   std::filesystem::copy_options::overwrite_existing);
        Reject(
            [&] {
                gengine::LoadGameInput(broken_source, false);
            },
            "dimension");
        const auto input = gengine::LoadGameInput(native, false);
        const auto input3 = gengine::LoadGameInput(native, true);
        auto baseline_handles = DWORD{0};
        auto baseline_memory = std::uint64_t{0};
        for (auto repeat = 0; repeat < 16; ++repeat) {
            const auto& current = repeat % 2 == 0 ? input : input3;
            const auto mode = repeat % 2 == 0 ? gengine::GameMode::k2d : gengine::GameMode::k3d;
            Reject(
                [&] {
                    gengine::PlayWindow window(nullptr, current.module, backends, mode, current.scene, false,
                                               &broken_source);
                },
                "fail.wav");
            {
                auto window =
                    gengine::PlayWindow(nullptr, current.module, backends, mode, current.scene, false, &packed);
                window.Capture();
            }
            Require(PlayWindows() == 0, "partial initialization leaked HWND");
            if (repeat == 7) {
                baseline_handles = Handles();
                baseline_memory = PrivateBytes();
            }
        }
        std::cout << "repeat handles=" << baseline_handles << "->" << Handles() << " private_bytes=" << baseline_memory
                  << "->" << PrivateBytes() << " process_peak_commit=" << PrivateBytes(true) << '\n';
        Require(Handles() <= baseline_handles + 4, "repeated loads leaked handles");
        Require(PrivateBytes() <= baseline_memory + 16 * 1024 * 1024, "repeated loads grew memory");
        std::cout << "asset parity, game result, corruption, bounded loading and rollback passed\n";
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        CoUninitialize();
        return 1;
    }
}
