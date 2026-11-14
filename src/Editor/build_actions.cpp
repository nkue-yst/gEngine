#include "editor_core.hpp"
#include <algorithm>

namespace gengine::editor
{
    std::optional<ActionError> EditorCore::CheckBuild(const BuildStart& request) const
    {
        if (const auto error = CheckCompile({request.project_id, request.expected_revision, request.source}); error) {
            return error;
        }
        if (std::find(project_->scenes.begin(), project_->scenes.end(), request.scene) == project_->scenes.end()) {
            return ActionError{"InvalidArgument", "scene", "scene is not registered in the project"};
        }
        if (request.choice && *request.choice != "save" && *request.choice != "cancel") {
            return ActionError{"InvalidArgument", "choice", "choose save or cancel"};
        }
        if (request.document_revision && *request.document_revision != scenes_.Revision()) {
            return ActionError{"Conflict", "document_revision", "scene revision changed"};
        }
        if (scenes_.document() && scenes_.State().dirty && request.choice != "cancel" &&
            (request.choice != "save" || request.document_revision.has_value() == false)) {
            return ActionError{"UnsavedChanges",
                               "choice",
                               "save the scene before building or cancel",
                               scenes_.Revision(),
                               {"save", "cancel"}};
        }
        return {};
    }

    Result<CompileAccepted> EditorCore::StartBuild(const BuildStart& request)
    {
        if (request.choice == "cancel") {
            return CompileAccepted{"Cancelled", "", request.expected_revision, ""};
        }
        try {
            if (Sha256(ReadFile(AllowedPath(project_->path), 64 * 1024)) != project_hash_) {
                return std::unexpected(ActionError{"Conflict", "path", "project changed; reopen it before building"});
            }
            const auto decision = scenes_.ResolveUnsaved(request.choice, request.document_revision);
            if (decision.has_value() == false) {
                return std::unexpected(decision.error());
            }

            // 所有スレッドで入力版を固定する。workerは文書やGUIを参照しない。
            auto input = BuildInput{};
            input.project_root = AllowedPath(project_->path).parent_path();
            input.tools = compiler_.parent_path();
            input.compiler = compiler_;
            input.source_name = request.source;
            input.source = ReadFile(ProjectPath(request.source), 1024 * 1024);
            input.scene = ReadFile(ProjectPath(request.scene), 1024 * 1024);
            input.project_hash = project_hash_;
            const auto id = session_id_ + "-" + std::to_string(++job_sequence_);
            std::filesystem::create_directory(ProjectPath(".gengine/jobs", true));
            std::filesystem::create_directory(ProjectPath(".gengine/builds", true));
            input.directory = ProjectPath(".gengine/jobs/" + id, true);
            if (std::filesystem::create_directory(input.directory) == false) {
                throw std::runtime_error("build job directory already exists");
            }

            auto job = JobInfo{};
            job.job_id = id;
            job.project_id = request.project_id;
            job.input_revision = request.expected_revision;
            job.source = request.source;
            job.source_hash = Sha256(input.source);
            job.kind = "Build";
            job.scene = request.scene;
            job.document_revision = scenes_.document() && scenes_.Path() == request.scene
                                        ? std::optional(scenes_.Revision())
                                        : std::nullopt;
            auto build = std::make_unique<BuildPipeline>(std::move(input));
            if (jobs_.size() >= 64) {
                const auto oldest = std::min_element(jobs_.begin(), jobs_.end(), [](const auto& a, const auto& b) {
                    return std::stoull(a.first.substr(a.first.rfind('-') + 1)) <
                           std::stoull(b.first.substr(b.first.rfind('-') + 1));
                });
                jobs_.erase(oldest);
            }
            const auto hash = job.source_hash;
            jobs_.emplace(id, std::move(job));
            build_ = std::move(build);
            active_job_ = id;
            return CompileAccepted{"Accepted", id, request.expected_revision, hash};
        } catch (const std::exception& error) {
            return std::unexpected(ActionError{"IoError", {}, error.what()});
        }
    }

    void EditorCore::TickBuild()
    {
        auto& job = jobs_.at(*active_job_);
        job.state = "Running";
        job.phase = job.cancel_requested ? "Cancelling" : build_->Phase();
        if (build_->Ready() == false) {
            return;
        }
        const auto result = build_->Get();
        job.phase = result.phase;
        job.game = result.game.empty() ? std::nullopt : std::optional(result.game);
        job.input_hash = result.input_hash.empty() ? std::nullopt : std::optional(result.input_hash);
        job.state = job.cancel_requested ? "Cancelled" : "Failed";
        try {
            if (job.cancel_requested == false && result.succeeded) {
                // 全工程の成功後に一度だけ公開する。過去の成功フォルダーは上書きしない。
                const auto source = ProjectPath(".gengine/jobs/" + job.job_id + "/product", true);
                const auto destination = ProjectPath(".gengine/builds/" + job.job_id, true);
                VerifyBuild(source, result.manifest_hash);
                if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) == FALSE) {
                    throw std::runtime_error("cannot publish completed build; previous artifacts preserved");
                }
                job.artifact = PathText(destination.lexically_relative(workspace_));
                job.artifact_hash = result.manifest_hash;
                job.state = "Succeeded";
                job.phase = "Finished";
            }
            else if (job.cancel_requested == false) {
                job.diagnostics.push_back({job.source, {}, {}, result.phase + ": " + result.diagnostic});
            }
        } catch (const std::exception& error) {
            job.phase = "Deploying";
            job.diagnostics.push_back({job.source, {}, {}, error.what()});
        }
        build_.reset();
        active_job_.reset();
    }

    void EditorCore::TickRunner()
    {
        if (runner_ == nullptr) {
            return;
        }
        runner_->Drain();
        if (WaitForSingleObject(runner_->process.Get(), 0) == WAIT_TIMEOUT) {
            return;
        }
        runner_->Drain();
        DWORD code = 1;
        GetExitCodeProcess(runner_->process.Get(), &code);
        runner_state_.exit_code = code;
        runner_state_.state = runner_state_.state == "Stopping" ? "Stopped" : code == 0 ? "Exited" : "Failed";
        runner_state_.diagnostic = runner_->log;
        runner_.reset();
    }

    void EditorCore::RegisterBuild(Registry& registry)
    {
        registry.Add<BuildStart, CompileAccepted>(
            "build.start", "保存済みシーンを検証・コンパイル・pack化して実行物を配置する", "build", true,
            [this](const BuildStart& request) {
                return CheckBuild(request);
            },
            [this](const BuildStart& request) {
                return StartBuild(request);
            });
        registry.Add<EmptyRequest, RunnerState>(
            "standalone.get_state", "最後のGameRunnerの実行・終了状態を取得する", "read", false,
            [](const EmptyRequest&) -> std::optional<ActionError> {
                return {};
            },
            [this](const EmptyRequest&) -> Result<RunnerState> {
                return runner_state_;
            });
        registry.Add<JobRequest, RunnerState>(
            "standalone.start", "今回の成功したビルドからGameRunnerを起動する", "runtime-control", true,
            [this](const JobRequest& request) -> std::optional<ActionError> {
                if (const auto error = Idle(); error) {
                    return error;
                }
                if (const auto error = CheckJob(request); error) {
                    return error;
                }
                const auto& job = jobs_.at(request.job_id);
                if (project_.has_value() == false || State().last_job != request.job_id || job.kind != "Build" ||
                    job.state != "Succeeded" || job.project_id != project_->project_id ||
                    job.input_revision != project_->revision || job.artifact.has_value() == false) {
                    return ActionError{"InvalidState", "job_id", "build the current project successfully first"};
                }
                return {};
            },
            [this](const JobRequest& request) -> Result<RunnerState> {
                try {
                    const auto& job = jobs_.at(request.job_id);
                    const auto directory = AllowedPath(*job.artifact, true);
                    const auto executable = VerifyBuild(directory, *job.artifact_hash);
                    auto child = std::make_unique<ChildProcess>();
                    child->Start(directory / executable,
                                 {L"--game", Wide(*job.game), L"--pack", (directory / "game.bpack").native(),
                                  L"--managed", L"1", L"--ephemeral", L"1"},
                                 directory);
                    runner_state_ = {NewId(), request.job_id, "Running", {}, ""};
                    runner_state_.process_id = GetProcessId(child->process.Get());
                    runner_ = std::move(child);
                    return runner_state_;
                } catch (const std::exception& error) {
                    runner_state_ = {NewId(), request.job_id, "Failed", {}, error.what()};
                    return std::unexpected(ActionError{"RuntimeError", "job_id", error.what()});
                }
            });
        registry.Add<RunnerRequest, RunnerState>(
            "standalone.stop", "Editorが起動したGameRunnerだけを停止する", "runtime-control", true,
            [this](const RunnerRequest& request) -> std::optional<ActionError> {
                if (runner_ == nullptr || runner_state_.run_id != request.run_id) {
                    return ActionError{"NotFound", "run_id", "runner is not active"};
                }
                return {};
            },
            [this](const RunnerRequest&) -> Result<RunnerState> {
                if (TerminateJobObject(runner_->group.Get(), 1) == FALSE) {
                    return std::unexpected(ActionError{"IoError", "run_id", "could not stop GameRunner"});
                }
                runner_state_.state = "Stopping";
                return runner_state_;
            });
    }
} // namespace gengine::editor
