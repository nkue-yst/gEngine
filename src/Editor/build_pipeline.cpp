#include "build_pipeline.hpp"
#include "assets.hpp"
#include "game.hpp"
#include "json.hpp"
#include "product.hpp"
#include "visual_manifest.hpp"
#include <aengine/audio.hpp>
#include <algorithm>
#include <set>

namespace gengine::editor
{
    namespace
    {
        constexpr auto kFileLimit = 64u * 1024 * 1024;
        constexpr auto kTotalLimit = 128u * 1024 * 1024;

        std::string Bytes(const AssetSource& source, const std::string& path, std::uint64_t limit)
        {
            auto metrics = AssetMetrics{};
            const auto data = source.Read(path, limit, metrics);
            return {reinterpret_cast<const char*>(data.data()), data.size()};
        }
    } // namespace

    BuildPipeline::BuildPipeline(BuildInput input) : input_(std::move(input))
    {
        result_ = std::async(std::launch::async, [this] {
            return Run();
        });
    }

    BuildPipeline::~BuildPipeline()
    {
        Cancel();
        if (result_.valid()) {
            result_.wait();
        }
        // このジョブが生成した既知の一時ファイルだけを除去する。公開済み成果物は別の場所にある。
        for (const auto& file : temporary_files_) {
            auto safe = true;
            // 作業領域が外部からリンクへ差し替えられた場合は、削除を進めず残す。
            for (auto parent = file.parent_path(); parent.empty() == false; parent = parent.parent_path()) {
                const auto attributes = GetFileAttributesW(parent.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    safe = false;
                    break;
                }
                if (parent == input_.directory) {
                    break;
                }
            }
            if (safe) {
                DeleteFileW(file.c_str());
            }
        }
        std::sort(temporary_directories_.begin(), temporary_directories_.end(), [](const auto& a, const auto& b) {
            return a.native().size() > b.native().size();
        });
        for (const auto& directory : temporary_directories_) {
            RemoveDirectoryW(directory.c_str());
        }
        RemoveDirectoryW(input_.directory.c_str());
    }

    void BuildPipeline::TrackFile(const std::filesystem::path& file)
    {
        temporary_files_.push_back(file);
        for (auto parent = file.parent_path(); parent != input_.directory && parent.empty() == false;
             parent = parent.parent_path()) {
            temporary_directories_.push_back(parent);
        }
    }

    void BuildPipeline::Cancel()
    {
        cancelled_.store(true);
    }

    std::string BuildPipeline::Phase() const
    {
        constexpr auto names = std::array{"Validating", "Compiling", "Packing", "Deploying"};
        return names.at(phase_.load());
    }

    bool BuildPipeline::Ready() const
    {
        return result_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    BuildOutput BuildPipeline::Get()
    {
        return result_.get();
    }

    void BuildPipeline::CheckCancelled() const
    {
        if (cancelled_.load()) {
            throw std::runtime_error("build cancelled");
        }
    }

    void BuildPipeline::RunTool(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                                const std::filesystem::path& cwd)
    {
        CheckCancelled();
        auto child = ChildProcess{};
        child.Start(executable, arguments, cwd);
        if (executable == input_.compiler) {
            TrackFile(std::filesystem::path(arguments.back() + L"." +
                                            std::to_wstring(GetProcessId(child.process.Get())) + L".tmp"));
        }
        while (WaitForSingleObject(child.process.Get(), 0) == WAIT_TIMEOUT) {
            child.Drain();
            CheckCancelled();
            if (std::chrono::steady_clock::now() - child.started > std::chrono::seconds(60)) {
                throw std::runtime_error("tool exceeded the 60 second deadline");
            }
            Sleep(5);
        }
        child.Drain();
        CheckCancelled();
        DWORD code = 1;
        if (GetExitCodeProcess(child.process.Get(), &code) == FALSE || code != 0) {
            throw std::runtime_error(child.log.empty() ? "tool failed without diagnostics" : child.log);
        }
    }

    BuildOutput BuildPipeline::Run()
    {
        auto result = BuildOutput{};
        try {
            // 保存済み文書と素材を専用領域へ固定し、コピーした内容をRuntimeの検証へ通す。
            CheckCancelled();
            auto scene = ParseScene(input_.scene);
            result.game = scene.game;
            ValidateSceneForPlay(scene);
            const auto content = input_.directory / "content";
            const auto product = input_.directory / "product";
            std::filesystem::create_directories(content / "scenes");
            std::filesystem::create_directory(product);
            temporary_directories_.insert(temporary_directories_.end(), {content / "scenes", content, product});
            const auto project = AssetSource(input_.project_root);
            auto definition = std::optional<ProductDefinition>{};
            auto icon = std::string{};
            auto hashes = std::map<std::string, std::string>{};
            hashes["project"] = input_.project_hash;
            hashes["source:" + input_.source_name] = Sha256(input_.source);
            hashes["scene"] = Sha256(input_.scene);
            if (project.Exists("product.json")) {
                const auto text = Bytes(project, "product.json", 4096);
                definition = DecodeProductDefinition(text);
                icon = Bytes(project, definition->icon, 1024 * 1024);
                hashes["product"] = Sha256(text);
                hashes["product_icon"] = Sha256(icon);
            }
            const auto module_name = definition ? std::string("game.glbc") : "shooter" + scene.game + ".glbc";
            const auto scene_name =
                definition ? std::string("scenes/game.json") : "scenes/shooter" + scene.game + ".json";
            auto total = std::uint64_t{};
            const auto write = [&](const std::string& name, const std::string& bytes) {
                CheckCancelled();
                total += bytes.size();
                if (total > kTotalLimit) {
                    throw std::runtime_error("build input exceeds 128 MiB");
                }
                const auto path = content / Wide(name);
                std::filesystem::create_directories(path.parent_path());
                TrackFile(path);
                WriteNewFile(path, bytes);
                hashes["content:" + name] = Sha256(bytes);
            };
            // packでは供給元が一つになるため、プロジェクト素材を専用の接頭辞で区別する。
            // 元の文書は変えず、ビルド用コピーだけを変換して標準音・描画との同名衝突を防ぐ。
            const auto defaults = AssetSource(input_.tools / "content");
            auto paths =
                std::set<std::string>{"assets/shot.wav", "assets/hit.wav", "assets/clear.wav", "assets/fail.wav",
                                      scene.game == "3d" ? "assets/sphere.gmesh" : "assets/sprite.png"};
            auto references = std::map<std::string, std::string>{};
            for (auto& object : scene.objects) {
                for (auto* reference : {&object.visual, &object.audio}) {
                    if (*reference) {
                        const auto original = **reference;
                        **reference = "project/" + original;
                        references[**reference] = original;
                        paths.insert(**reference);
                    }
                }
            }
            for (const auto& path : paths) {
                write(path, references.contains(path) ? Bytes(project, references.at(path), 16 * 1024 * 1024)
                                                      : Bytes(defaults, path, 16 * 1024 * 1024));
            }
            write(scene_name, SerializeScene(scene));
            const auto snapshot = AssetSource(content);
            for (const auto& path : paths) {
                const auto inspected = InspectAsset(snapshot, path);
                if (inspected.diagnostic.empty() == false) {
                    throw std::runtime_error(path + ": " + inspected.diagnostic);
                }
            }
            for (const auto& reference : InspectSceneAssets(scene, snapshot)) {
                if (reference.diagnostic.empty() == false) {
                    throw std::runtime_error(reference.object_id + ": " + reference.diagnostic);
                }
            }

            // 素材検査を終えた後、製品の入口を同じスナップショットへ固定する。
            if (definition) {
                write("game.json", SerializeGameDefinition({definition->title, module_name, scene_name}));
                paths.insert("game.json");
            }

            // 元の相対ソース名でコンパイルし、生成物の公開関数と次元も検証する。
            phase_.store(1);
            const auto source_root = input_.directory / "source";
            const auto source_file = source_root / Wide(input_.source_name);
            std::filesystem::create_directories(source_file.parent_path());
            TrackFile(source_file);
            WriteNewFile(source_file, input_.source);
            TrackFile(content / module_name);
            RunTool(input_.compiler, {Wide(input_.source_name), L"-o", (content / module_name).native()}, source_root);
            auto game_input = LoadGameInput(snapshot, scene.game == "3d");
            auto game =
                Game(game_input.module, 1000000, scene.game == "3d" ? GameMode::k3d : GameMode::k2d, game_input.scene);
            // 選択APIを使うゲームは同じUI入力をPIE・packへ渡す。欠落はビルド失敗にする。
            if (game.HasUi()) {
                for (const auto* path : {"ui/game.json", "ui/font.png"}) {
                    write(path, Bytes(project, path, 16 * 1024 * 1024));
                    paths.insert(path);
                }
                const auto ui = Json::parse(Bytes(project, "ui/game.json", 128 * 1024));
                if (ui.value("audio_settings", 0u) == 1) {
                    auto metrics = AssetMetrics{};
                    auto audio = DecodeAudioManifest(project.Read("audio/game.json", 16384, metrics));
                    for (auto index = 0u; index < 2; ++index) {
                        const auto kind = index == 0 ? "player_spawn" : "enemy_spawn";
                        const auto object =
                            std::find_if(scene.objects.begin(), scene.objects.end(), [&](const auto& value) {
                                return value.kind == kind;
                            });
                        if (object != scene.objects.end() && object->audio) {
                            audio.events[index] = references.at(*object->audio);
                        }
                    }
                    auto total_pcm = std::uint64_t{0};
                    // 音源の参照もproject配下へ変換し、通常素材と同名でも混在させない。
                    const auto copy_audio = [&](std::string& path, std::uint64_t limit) {
                        if (path.empty()) {
                            return;
                        }
                        const auto bytes = Bytes(project, path, limit);
                        const auto pcm = aengine::InspectWav(std::as_bytes(std::span(bytes)));
                        if (AssetKind(path) != "audio" || pcm.has_value() == false) {
                            throw std::runtime_error(path + ": invalid PCM16 WAV");
                        }
                        total_pcm += pcm->bytes.size();
                        if (total_pcm > 64 * 1024 * 1024) {
                            throw std::runtime_error("game audio exceeds 64 MiB PCM budget");
                        }
                        path = "project/" + path;
                        if (paths.insert(path).second) {
                            write(path, bytes);
                        }
                    };
                    for (auto& path : audio.events) {
                        copy_audio(path, 1024 * 1024);
                    }
                    for (auto& path : audio.tracks) {
                        copy_audio(path, 16 * 1024 * 1024);
                    }
                    write("audio/game.json", SerializeAudioManifest(audio));
                    paths.insert("audio/game.json");
                }
            }
            if (project.Exists("visual/game.json")) {
                if (scene.game != "2d") {
                    throw std::runtime_error("visual atlas currently requires a 2D game");
                }
                auto metrics = AssetMetrics{};
                auto visual = DecodeVisualManifest(project.Read("visual/game.json", 16384, metrics));
                // 通常ファイルと同じPNG検査を行い、pack内の参照だけを専用接頭辞へ変換する。
                for (auto* path : {&visual.backdrop, &visual.title_logo, &visual.atlas, &visual.detail_atlas,
                                   &visual.environment_atlas}) {
                    if (path->empty()) {
                        continue;
                    }
                    const auto bytes = project.Read(*path, 16 * 1024 * 1024, metrics);
                    DecodePng(bytes);
                    *path = "project/" + *path;
                    if (paths.insert(*path).second) {
                        write(*path, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                    }
                }
                write("visual/game.json", SerializeVisualManifest(visual));
                paths.insert("visual/game.json");
            }
            result.input_hash = Sha256(Json(hashes).dump());

            // 既存bpackコマンドへスナップショットだけを渡し、作成後も同じ読込み経路で確認する。
            phase_.store(2);
            const auto pack = product / "game.bpack";
            TrackFile(pack);
            RunTool(input_.tools / "bpack.exe",
                    {L"pack", pack.native(), L"--base", content.native(), L"--compress", content.native()},
                    input_.directory);
            const auto packed = AssetSource(pack, true);
            const auto packed_input = LoadGameInput(packed, scene.game == "3d");
            if (packed_input.module != game_input.module || packed_input.title != game_input.title ||
                SerializeScene(packed_input.scene) != SerializeScene(scene)) {
                throw std::runtime_error("packed game differs from build input");
            }
            for (const auto& path : paths) {
                if (Sha256(Bytes(packed, path, 16 * 1024 * 1024)) != hashes.at("content:" + path)) {
                    throw std::runtime_error("packed asset differs: " + path);
                }
            }

            // 実行に必要な固定一覧だけを配置し、コンパイラーやEditor・Qtは含めない。
            phase_.store(3);
            const auto tools = AssetSource(input_.tools);
            const auto executable = definition ? definition->executable : "gengine_runner.exe";
            auto files = std::map<std::string, std::string>{};
            for (const auto* path :
                 {"gengine_runner.exe", "backends/aengine_backend_xaudio2.dll", "backends/rengine_backend_d3d12.dll",
                  "ThirdParty/aEngine-LICENSE.txt", "ThirdParty/rEngine-LICENSE.txt",
                  "ThirdParty/BinPackLib-LICENSE.txt", "ThirdParty/gLang-LICENSE.txt",
                  "ThirdParty/nlohmann-json-LICENSE.MIT"}) {
                CheckCancelled();
                const auto bytes = Bytes(tools, path, kFileLimit);
                const auto target_name =
                    std::string_view(path) == "gengine_runner.exe" ? executable : std::string(path);
                const auto target = product / target_name;
                std::filesystem::create_directories(target.parent_path());
                TrackFile(target);
                WriteNewFile(target, bytes);
                if (definition && std::string_view(path) == "gengine_runner.exe") {
                    EmbedProductResources(target, {reinterpret_cast<const std::uint8_t*>(icon.data()), icon.size()});
                    files[target_name] = Sha256(ReadFile(target, kFileLimit));
                }
                else {
                    files[target_name] = Sha256(bytes);
                }
            }
            files["game.bpack"] = Sha256(ReadFile(pack, kTotalLimit));
            const auto manifest = Json{{"format_version", 1},      {"game", scene.game},
                                       {"executable", executable}, {"input_hash", result.input_hash},
                                       {"inputs", hashes},         {"files", files}}
                                      .dump(2);
            TrackFile(product / "build.json");
            WriteNewFile(product / "build.json", manifest);
            result.manifest_hash = Sha256(manifest);
            CheckCancelled();
            result.succeeded = true;
        } catch (const std::exception& error) {
            result.diagnostic = std::string(error.what()).substr(0, 4096);
        }
        result.phase = Phase();
        return result;
    }

    std::string VerifyBuild(const std::filesystem::path& directory, std::string_view manifest_hash)
    {
        const auto source = AssetSource(directory);
        const auto text = Bytes(source, "build.json", 1024 * 1024);
        if (Sha256(text) != manifest_hash) {
            throw std::runtime_error("build manifest changed; build again");
        }
        const auto manifest = Json::parse(text);
        const auto executable = manifest.value("executable", std::string("gengine_runner.exe"));
        ValidateProductExecutable(executable);
        if (manifest.at("files").contains(executable) == false) {
            throw std::runtime_error("build manifest has no executable");
        }
        for (const auto& [path, hash] : manifest.at("files").items()) {
            if (Sha256(Bytes(source, path, kTotalLimit)) != hash.get<std::string>()) {
                throw std::runtime_error("build artifact changed: " + path);
            }
        }
        return executable;
    }
} // namespace gengine::editor
