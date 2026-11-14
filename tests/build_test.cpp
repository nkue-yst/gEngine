#include "editor_core.hpp"
#include "json.hpp"
#include <iostream>

using namespace gengine::editor;

namespace
{
    void Require(bool condition, const char* reason)
    {
        if (condition == false) {
            throw std::runtime_error(reason);
        }
    }
    template <class T> Operation Act(EditorCore& core, const char* name, const T& value)
    {
        const auto accepted = core.dispatcher().SubmitTyped(name, value, NewId());
        Require(accepted.has_value(), "action not accepted");
        core.dispatcher().Pump();
        return *core.dispatcher().Lookup(accepted->operation_id).operation;
    }
    template <class T, class I> T Call(EditorCore& core, const char* name, const I& value)
    {
        const auto result = Act(core, name, value);
        if (result.error) {
            throw std::runtime_error(std::string(name) + ": " + result.error->reason);
        }
        return Decode<T>(result.result).value();
    }
    JobInfo Wait(EditorCore& core, const std::string& id)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        while (std::chrono::steady_clock::now() < deadline) {
            core.Tick();
            const auto job = Call<JobInfo>(core, "job.get", JobRequest{id});
            if (job.state != "Running" && job.state != "Queued") {
                return job;
            }
            Sleep(5);
        }
        throw std::runtime_error("build deadline");
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    try {
        Require(argc == 4, "expected root, tools, slow compiler");
        const auto root = std::filesystem::absolute(argv[1]) / Wide(NewId());
        const auto original = std::filesystem::path(argv[2]);
        const auto tools = root / L"tools 日本語";
        std::filesystem::create_directories(tools);
        // 失敗注入は専用の小さなツール配置だけを変更し、開発用Editorの実行物を保つ。
        for (const auto* file : {"glangc.exe", "bpack.exe", "gengine_runner.exe"}) {
            std::filesystem::copy_file(original / file, tools / file);
        }
        for (const auto* directory : {"content", "backends", "ThirdParty"}) {
            std::filesystem::create_directory(tools / directory);
        }
        std::filesystem::copy(original / "content", tools / "content", std::filesystem::copy_options::recursive);
        for (const auto* file : {"backends/aengine_backend_xaudio2.dll", "backends/rengine_backend_d3d12.dll",
                                 "ThirdParty/aEngine-LICENSE.txt", "ThirdParty/rEngine-LICENSE.txt",
                                 "ThirdParty/BinPackLib-LICENSE.txt", "ThirdParty/gLang-LICENSE.txt",
                                 "ThirdParty/nlohmann-json-LICENSE.MIT"}) {
            std::filesystem::copy_file(original / file, tools / file);
        }
        auto core = EditorCore(root, tools / "glangc.exe");
        for (const auto* mode : {"2d", "3d"}) {
            const auto name = std::string("日本語 project ") + mode;
            const auto created = Call<EditorState>(core, "project.create", ProjectCreateRequest{name, mode});
            const auto project = *created.project;
            auto request = BuildStart{project.project_id, project.revision, project.sources[0], project.scenes[0]};
            const auto directory = root / Wide(name);
            auto document = Call<DocumentInfo>(core, "document.open", DocumentOpen{request.scene});
            std::filesystem::create_directory(directory / "assets");
            const auto custom_sound = ReadFile(tools / "content/assets/hit.wav", 1024 * 1024);
            WriteNewFile(directory / "assets/shot.wav", custom_sound);
            auto edit = SceneBatch{document.document_id,
                                   document.revision,
                                   {{"settings", {}, {}, Value::Object{{"player_hp", 4u}}},
                                    {"set", "player", {}, Value::Object{{"audio", "assets/shot.wav"}}}}};
            // 未保存の確認は既存の保存処理へ接続し、取消しではジョブも生成しない。
            document = Call<DocumentInfo>(core, "scene.apply_batch", edit);
            Require(Act(core, "build.start", request).error->code == "UnsavedChanges", "dirty build accepted");
            request.choice = "cancel";
            Require(Call<CompileAccepted>(core, "build.start", request).status == "Cancelled", "cancel failed");
            request.choice = "save";
            request.document_revision = document.revision;
            const auto accepted = Call<CompileAccepted>(core, "build.start", request);
            edit.expected_revision = document.revision;
            Require(Act(core, "scene.apply_batch", edit).error->code == "Busy", "editing during build accepted");
            const auto successful = Wait(core, accepted.job_id);
            if (successful.state != "Succeeded") {
                throw std::runtime_error(ToJson(Encode(successful)));
            }
            Require(core.scenes().State().dirty == false, "save before build missing");
            const auto product = root / Wide(*successful.artifact);
            VerifyBuild(product, *successful.artifact_hash);
            const auto content = gengine::AssetSource(product / "game.bpack", true);
            const auto input = gengine::LoadGameInput(content, std::string(mode) == "3d");
            Require(input.scene.document_id == document.document_id, "wrong packed scene");
            auto metrics = gengine::AssetMetrics{};
            const auto standard = content.Read("assets/shot.wav", 1024 * 1024, metrics);
            const auto custom = content.Read("project/assets/shot.wav", 1024 * 1024, metrics);
            Require(std::string(custom.begin(), custom.end()) == custom_sound && custom != standard,
                    "explicit reference replaced standard sound");
            Require(std::filesystem::exists(product / "glangc.exe") == false &&
                        std::filesystem::exists(product / "content") == false,
                    "build contains development inputs");

            // 各段階で失敗させ、成果物なし・理由あり・旧成功の起動不可を確認する。
            const auto source_path = directory / Wide(request.source);
            const auto source = ReadFile(source_path, 1024 * 1024);
            WriteAtomicFile(source_path, "broken source");
            auto failed = Wait(core, Call<CompileAccepted>(core, "build.start", request).job_id);
            Require(failed.state == "Failed" && failed.phase == "Compiling" && failed.artifact.has_value() == false,
                    "compile failure hidden");
            Require(Act(core, "standalone.start", JobRequest{successful.job_id}).error.has_value(),
                    "old build launched");
            VerifyBuild(product, *successful.artifact_hash);
            WriteAtomicFile(source_path, source);
            for (const auto& failure :
                 std::array{std::pair{"bpack.exe", "Packing"}, std::pair{"gengine_runner.exe", "Deploying"},
                            std::pair{"content/assets/shot.wav", "Validating"}}) {
                const auto path = tools / failure.first;
                const auto held = std::filesystem::path(path.native() + L".held");
                std::filesystem::rename(path, held);
                failed = Wait(core, Call<CompileAccepted>(core, "build.start", request).job_id);
                std::filesystem::rename(held, path);
                Require(failed.state == "Failed" && failed.phase == failure.second &&
                            failed.artifact.has_value() == false && failed.diagnostics.empty() == false,
                        "pipeline failure hidden");
            }

            // 受付後に元ソースを変えても、コンパイルは受付済みスナップショットを使う。
            const auto snapshot = Call<CompileAccepted>(core, "build.start", request);
            WriteAtomicFile(source_path, "changed after acceptance");
            const auto frozen = Wait(core, snapshot.job_id);
            WriteAtomicFile(source_path, source);
            Require(frozen.state == "Succeeded" && frozen.input_hash == successful.input_hash &&
                        frozen.artifact_hash == successful.artifact_hash,
                    "snapshot or identical build output changed");
            const auto changed_product = root / Wide(*frozen.artifact);
            WriteAtomicFile(changed_product / "game.bpack", "corrupt");
            Require(Act(core, "standalone.start", JobRequest{frozen.job_id}).error.has_value(),
                    "tampered build launched");

            const auto cancelled = Call<CompileAccepted>(core, "build.start", request);
            Call<JobInfo>(core, "job.cancel", JobRequest{cancelled.job_id});
            Require(Wait(core, cancelled.job_id).state == "Cancelled", "build cancellation failed");
            Require(std::filesystem::is_empty(directory / ".gengine/jobs"), "temporary build files retained");
            Call<EditorState>(core, "project.close", CloseRequest{project.project_id, project.revision});
            std::cout << mode
                      << ": saved build, snapshot, all failure phases, stale/tampered rejection, cancel passed\n";
        }

        // 実行中のcompilerを取り消し、worker破棄後に子が残らずプロジェクトを再利用できる。
        std::filesystem::copy_file(argv[3], tools / "glangc.exe", std::filesystem::copy_options::overwrite_existing);
        const auto project = *Call<EditorState>(core, "project.create", ProjectCreateRequest{"slow", "2d"}).project;
        const auto accepted = Call<CompileAccepted>(
            core, "build.start",
            BuildStart{project.project_id, project.revision, project.sources[0], project.scenes[0]});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (Call<JobInfo>(core, "job.get", JobRequest{accepted.job_id}).phase != "Compiling") {
            Require(std::chrono::steady_clock::now() < deadline, "slow compiler not reached");
            core.Tick();
            Sleep(10);
        }
        Sleep(100);
        core.Shutdown();
        auto reopened = EditorCore(root, original / "glangc.exe");
        Call<EditorState>(reopened, "project.open", ProjectPathRequest{"slow/project.gengine.json"});
        std::cout << "active build shutdown and project lock release passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
