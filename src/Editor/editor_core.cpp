#include "editor_core.hpp"
#include "json.hpp"
#include "project_template.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <set>
#include <stdexcept>

namespace gengine::editor
{
    namespace
    {
        // ファイルやJSONの例外を、GUIとMCPで共通して扱える操作エラーに変換する。
        template <class Function> auto Guarded(Function function) -> decltype(function())
        {
            try {
                return function();
            } catch (const Json::exception& error) {
                return std::unexpected(ActionError{"InvalidArgument", {}, std::string(error.what()).substr(0, 2048)});
            } catch (const std::invalid_argument& error) {
                return std::unexpected(ActionError{"InvalidArgument", {}, error.what()});
            } catch (const std::exception& error) {
                return std::unexpected(ActionError{"IoError", {}, std::string(error.what()).substr(0, 2048)});
            }
        }

        bool ValidName(std::string_view name)
        {
            if (name.empty() || name.size() > 128) {
                return false;
            }
            return std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                       c == '_' || c == '.';
            });
        }
    } // namespace

    // 一回のコンパイルが使う一時ファイルと子プロセスをまとめて所有する。
    struct EditorCore::CompileProcess : ChildProcess
    {
        std::filesystem::path directory;
        std::filesystem::path input;
        std::filesystem::path input_argument;
        std::filesystem::path output;
        std::filesystem::path destination;
        std::filesystem::path compiler_temporary;

        std::string snapshot;
        bool timed_out = false;

        ~CompileProcess()
        {
            // 終了時に子だけが残らないよう、Job Objectで終了させてから一時入力を片付ける。
            Stop();

            process.Reset();
            group.Reset();
            pipe.Reset();

            // このジョブが作ったファイルと空の親フォルダーだけを除去する。
            if (directory.empty() == false) {
                DeleteFileW(input.c_str());
                auto parent = input.parent_path();
                while (parent != directory && parent.empty() == false) {
                    RemoveDirectoryW(parent.c_str());
                    parent = parent.parent_path();
                }

                DeleteFileW(output.c_str());
                if (compiler_temporary.empty() == false) {
                    DeleteFileW(compiler_temporary.c_str());
                }
                RemoveDirectoryW(directory.c_str());
            }
        }

        void Start(const std::filesystem::path& compiler)
        {
            ChildProcess::Start(compiler, {input_argument.native(), L"-o", output.native()}, directory);
            compiler_temporary = output;
            compiler_temporary += L"." + std::to_wstring(GetProcessId(process.Get())) + L".tmp";
        }
    };

    EditorCore::EditorCore(std::filesystem::path workspace, std::filesystem::path compiler, bool automatic_workspace)
        : workspace_(std::filesystem::canonical(workspace)), compiler_(std::filesystem::absolute(compiler)),
          automatic_workspace_(automatic_workspace), session_id_(NewId()), dispatcher_(Definitions())
    {
        if (std::filesystem::is_directory(workspace_) == false) {
            throw std::invalid_argument("workspace must be an existing directory");
        }
    }

    EditorCore::~EditorCore()
    {
        Shutdown();
    }

    Registry EditorCore::Definitions()
    {
        auto registry = Registry{};
        const auto readable = [](const auto&) -> std::optional<ActionError> {
            return {};
        };

        // 状態照会はプロジェクトが閉じているときも利用できる。
        registry.Add<EmptyRequest, std::vector<ActionInfo>>(
            "action.list", "利用できる操作定義を取得する", "read", false, readable,
            [this](const EmptyRequest&) -> Result<std::vector<ActionInfo>> {
                return dispatcher_.List();
            });
        registry.Add<AvailabilityRequest, AvailabilityResult>(
            "action.get_availability", "操作できる状態か確認する", "read", false, readable,
            [this](const AvailabilityRequest& request) -> Result<AvailabilityResult> {
                const auto error = dispatcher_.Availability(request.action_id, request.version, request.arguments);
                return AvailabilityResult{error.has_value() == false, error};
            });
        registry.Add<OperationRequest, OperationLookup>(
            "operation.get", "以前の要求の結果を取得する", "read", false, readable,
            [this](const OperationRequest& request) -> Result<OperationLookup> {
                return dispatcher_.Lookup(request.operation_id);
            });
        registry.Add<EmptyRequest, EditorState>("editor.get_state", "プロジェクトと実行中ジョブを取得する", "read",
                                                false, readable, [this](const EmptyRequest&) -> Result<EditorState> {
                                                    return State();
                                                });

        // 新規作成と読込みは、実行中のジョブも開いているプロジェクトもないときに限る。
        const auto can_open = [this](const auto&) -> std::optional<ActionError> {
            if (const auto error = Idle(); error.has_value()) {
                return error;
            }

            if (project_.has_value()) {
                return ActionError{"InvalidState", "path", "close the current project before opening another"};
            }
            return {};
        };

        registry.Add<ProjectCreateRequest, EditorState>("project.create", "新しいフォルダーにプロジェクトを作る",
                                                        "file-output", true, can_open,
                                                        [this](const ProjectCreateRequest& request) {
                                                            return Guarded([&] {
                                                                return CreateProject(request);
                                                            });
                                                        });

        registry.Add<ProjectPathRequest, EditorState>("project.open", "プロジェクトを検証して開く", "project-control",
                                                      true, can_open, [this](const ProjectPathRequest& request) {
                                                          return Guarded([&] {
                                                              return OpenProject(request);
                                                          });
                                                      });

        // 現在のプロジェクトを変更する操作は、要求側が見ていたIDと版を確認する。
        registry.Add<CloseRequest, EditorState>(
            "project.close", "プロジェクトを閉じる", "project-control", true,
            [this](const CloseRequest& request) {
                return CheckProject(request.project_id, request.expected_revision);
            },
            [this](const CloseRequest& request) {
                return Guarded([&] {
                    return CloseProject(request);
                });
            });

        registry.Add<CompileRequest, CompileAccepted>(
            "compile.start", "ソースを固定して非同期コンパイルを開始する", "file-output", true,
            [this](const CompileRequest& request) {
                return CheckCompile(request);
            },
            [this](const CompileRequest& request) {
                return Guarded([&] {
                    return Compile(request);
                });
            });

        // コンパイルの受付後は、ジョブIDで状態照会と取消しを行う。
        registry.Add<JobRequest, JobInfo>(
            "job.get", "ジョブの状態・診断・成果物を取得する", "read", false,
            [this](const JobRequest& request) {
                return CheckJob(request);
            },
            [this](const JobRequest& request) -> Result<JobInfo> {
                return jobs_.at(request.job_id);
            });

        registry.Add<JobRequest, JobInfo>(
            "job.cancel", "ジョブの取消しを要求する", "runtime-control", true,
            [this](const JobRequest& request) {
                return CheckJob(request);
            },
            [this](const JobRequest& request) {
                return Cancel(request);
            });
        // 単独ホストと同じVM操作を、Editorの操作一覧へ組み込む。
        vm_actions_.RegisterVmActions(registry);
        registry.Add<VmLoadRequest, EditorState>(
            "vm.load", "最新の成功した成果物を検証してVMへ読み込む", "runtime-control", true,
            [this](const VmLoadRequest& request) {
                return CheckLoad(request);
            },
            [this](const VmLoadRequest& request) {
                return Guarded([&] {
                    return LoadVm(request);
                });
            });
        registry.Add<VmRequest, EditorState>(
            "vm.unload", "読み込んだVMを解放する", "runtime-control", true,
            [this](const VmRequest& request) -> std::optional<ActionError> {
                if (vm_id_ != request.vm_id) {
                    return ActionError{"NotFound", "vm_id", "VM is not loaded"};
                }
                return {};
            },
            [this](const VmRequest&) -> Result<EditorState> {
                ReleaseVm();
                return State();
            });
        scenes_.Register(registry);
        RegisterAssets(registry);
        RegisterBuild(registry);
        RegisterPlay(registry);
        RegisterViewport(registry);
        return registry;
    }

    EditorState EditorCore::State() const
    {
        const auto last = session_id_ + "-" + std::to_string(job_sequence_);
        auto project = project_;
        const auto document = scenes_.document() ? std::optional(scenes_.State()) : std::nullopt;
        if (project) {
            project->dirty = document && document->dirty;
        }
        return {session_id_, project, active_job_, jobs_.contains(last) ? std::optional(last) : std::nullopt,
                64,          vm_id_,  loaded_job_, document};
    }

    std::optional<ActionError> EditorCore::Idle() const
    {
        if (play_ != nullptr || RunnerActive()) {
            return ActionError{"Busy", {}, "stop play before changing the project"};
        }
        if (stopping_) {
            return ActionError{"InvalidState", {}, "editor is shutting down"};
        }

        if (active_job_.has_value()) {
            return ActionError{"Busy", {}, "wait for the active job to finish or cancel it"};
        }
        return {};
    }

    std::optional<ActionError> EditorCore::CheckProject(std::string_view id, std::uint64_t revision) const
    {
        if (const auto error = Idle(); error.has_value()) {
            return error;
        }

        // 閉じて開き直した後に届いた古い要求を、そのまま実行しない。
        if (project_.has_value() == false || project_->project_id != id) {
            return ActionError{"NotFound", "project_id", "project is not open"};
        }
        if (project_->revision != revision) {
            return ActionError{"Conflict", "expected_revision", "project revision changed; read editor.get_state"};
        }
        return {};
    }

    std::optional<ActionError> EditorCore::CheckCompile(const CompileRequest& request) const
    {
        if (const auto error = CheckProject(request.project_id, request.expected_revision); error.has_value()) {
            return error;
        }

        if (std::find(project_->sources.begin(), project_->sources.end(), request.source) == project_->sources.end()) {
            return ActionError{"InvalidArgument", "source", "source is not registered in the project"};
        }
        return {};
    }

    std::optional<ActionError> EditorCore::CheckJob(const JobRequest& request) const
    {
        if (jobs_.contains(request.job_id) == false) {
            return ActionError{"NotFound", "job_id", "job is not in the retained history (latest 64 jobs)"};
        }
        return {};
    }

    std::filesystem::path EditorCore::AllowedPath(std::string_view relative, bool internal) const
    {
        // 起動後にworkspace自体が削除・リンクへ差替えされていないか確認する。
        const DWORD root_attributes = GetFileAttributesW(workspace_.c_str());
        if (root_attributes == INVALID_FILE_ATTRIBUTES || (root_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            throw std::invalid_argument("workspace was removed or replaced by a link");
        }

        // 相対パスだけを受け付け、Windowsで別の意味になる区切りや文字を拒否する。
        if (relative.empty() || relative.size() > 1024 || relative.find('\0') != std::string_view::npos ||
            relative.find_first_of("\\:*?\"<>|") != std::string_view::npos) {
            throw std::invalid_argument("use a relative path with forward slashes inside workspace");
        }
        const auto path = std::filesystem::path(Wide(relative));
        if (path.is_absolute() || path.has_root_path()) {
            throw std::invalid_argument("absolute project references are not allowed");
        }

        auto result = workspace_;
        for (const auto& component : path) {
            // 親への移動や予約領域への参照を、パスの各要素で検査する。
            const auto name = PathText(component);
            if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
                (internal == false && _wcsicmp(component.c_str(), L".gengine") == 0)) {
                throw std::invalid_argument("invalid or reserved project path component");
            }

            // 拡張子や大文字小文字で、デバイス名の検査をすり抜けさせない。
            auto base = name.substr(0, name.find('.'));
            std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
                (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '0' &&
                 base[3] <= '9') ||
                std::any_of(name.begin(), name.end(), [](unsigned char c) {
                    return c < 32;
                })) {
                throw std::invalid_argument("Windows device names and control characters are not allowed");
            }

            // 途中のフォルダーも調べ、リンク経由でworkspaceの外へ出る参照を拒否する。
            result /= component;
            const DWORD attributes = GetFileAttributesW(result.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                throw std::invalid_argument("junctions and symbolic links are not allowed inside workspace");
            }
        }
        return result;
    }

    std::filesystem::path EditorCore::ProjectPath(std::string_view relative, bool internal) const
    {
        const auto parent = std::filesystem::path(Wide(project_->path)).parent_path();
        return AllowedPath(PathText(parent / std::filesystem::path(Wide(relative))), internal);
    }

    Result<EditorState> EditorCore::CreateProject(const ProjectCreateRequest& request)
    {
        const auto kind = request.template_name.value_or("vm");
        if (kind != "vm" && kind != "2d" && kind != "3d") {
            return std::unexpected(ActionError{"InvalidArgument", "template_name", "choose vm, 2d or 3d"});
        }
        // 既存フォルダーを上書きせず、指定された一階層だけを新しく作る。
        const auto root = AllowedPath(request.path);
        if (std::filesystem::exists(root) || std::filesystem::is_directory(root.parent_path()) == false) {
            return std::unexpected(
                ActionError{"Conflict", "path", "choose a new folder directly below an existing workspace folder"});
        }

        // 初期ソースと設定を保存し、通常の読込み経路でプロジェクトを開く。
        std::filesystem::create_directory(root);
        std::filesystem::create_directory(root / L"scripts");
        WriteNewFile(root / L"scripts/game.glang", kind == "vm"   ? kProjectTemplate
                                                   : kind == "3d" ? kGame3dTemplate
                                                                  : kGame2dTemplate);
        auto document = ProjectFile{1, NewId(), {"scripts/game.glang"}, {}};
        if (kind != "vm") {
            auto scene = DefaultScene(kind == "3d");
            scene.document_id = NewId();
            WriteNewFile(root / L"scene.json", SerializeScene(scene));
            document.scenes = {"scene.json"};
        }
        WriteNewFile(root / L"project.gengine.json", AsJson(Encode(document)).dump(2) + "\n");
        return OpenProject({request.path + "/project.gengine.json"});
    }

    Result<EditorState> EditorCore::OpenProject(const ProjectPathRequest& request)
    {
        const auto selected = std::filesystem::path(Wide(request.path));
        if (automatic_workspace_ == false || selected.is_absolute() == false) {
            return ReadProject(request);
        }

        // GUIで選んだ設定ファイルの親を基準にし、参照先には従来の相対パス検証を適用する。
        auto previous_workspace = workspace_;
        workspace_ = std::filesystem::canonical(selected.parent_path());
        auto opened = Guarded([&] {
            return ReadProject({PathText(selected.filename())});
        });

        // 設定不正・参照先の欠落・他Editorのロックでも、元の作業場所を失わない。
        if (opened.has_value() == false) {
            workspace_.swap(previous_workspace);
        }
        return opened;
    }

    Result<EditorState> EditorCore::ReadProject(const ProjectPathRequest& request)
    {
        // JSONの容量・型・項目を検査し、対応しているプロジェクト形式か確認する。
        const auto path = AllowedPath(request.path);
        const auto bytes = ReadFile(path, 64 * 1024);
        const auto decoded = Decode<ProjectFile>(FromJson(ParseJson(bytes, 64 * 1024)));
        if (decoded.has_value() == false) {
            return std::unexpected(decoded.error());
        }

        const auto& document = *decoded;
        if (document.format_version != 1 || ValidName(document.project_id) == false || document.sources.empty() ||
            document.sources.size() > 64 || document.scenes.size() > 64 ||
            (document.assets && document.assets->size() > 256)) {
            return std::unexpected(
                ActionError{"InvalidArgument", "path", "unsupported project version, ID or source/scene count"});
        }

        // ソースの実在と重複を確認する。同じ実体への別名も重複として扱う。
        const auto parent = std::filesystem::path(Wide(request.path)).parent_path();
        auto seen = std::set<std::filesystem::path>{};
        for (const auto& source : document.sources) {
            const auto full = AllowedPath(PathText(parent / std::filesystem::path(Wide(source))));
            const bool duplicate = std::any_of(seen.begin(), seen.end(), [&](const auto& previous) {
                return std::filesystem::equivalent(previous, full);
            });
            if (std::filesystem::path(Wide(source)).is_relative() == false || full.extension() != L".glang" ||
                std::filesystem::is_regular_file(full) == false || duplicate) {
                return std::unexpected(
                    ActionError{"InvalidArgument", "sources", "sources must be distinct existing .glang files"});
            }
            seen.insert(full);
        }

        // この版ではシーンを読み込まず、参照先が許可範囲に収まることだけを確認する。
        for (const auto& scene : document.scenes) {
            AllowedPath(PathText(parent / std::filesystem::path(Wide(scene))));
        }

        // 排他的にロックファイルを開き、別のEditorとの同時利用を防ぐ。
        const auto control = AllowedPath(PathText(parent / L".gengine"), true);
        std::filesystem::create_directory(control);
        const auto lock_path = AllowedPath(PathText(parent / L".gengine/editor.lock"), true);
        auto lock = Handle(CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr));
        if (lock.Valid() == false) {
            return std::unexpected(ActionError{"Busy", "path",
                                               "project is open in another editor or "
                                               "its work directory is not writable"});
        }

        // 検証を全て通してから現在の状態を確定する。
        project_hash_ = Sha256(bytes);
        project_ = ProjectInfo{document.project_id,
                               ++revision_,
                               request.path,
                               document.sources,
                               document.scenes,
                               false,
                               document.assets.value_or(std::vector<std::string>{})};
        project_lock_ = std::move(lock);
        scenes_.Attach(
            path.parent_path(), document.scenes,
            [this](std::string_view relative, bool internal) {
                return ProjectPath(relative, internal);
            },
            [this] {
                return play_ != nullptr || RunnerActive() || build_ != nullptr;
            });
        return State();
    }

    Result<EditorState> EditorCore::CloseProject(const CloseRequest& request)
    {
        auto decision = scenes_.ResolveUnsaved(request.choice, request.document_revision);
        if (decision.has_value() == false) {
            return std::unexpected(decision.error());
        }
        if (decision->status == "Cancelled") {
            return State();
        }
        preview_.reset();
        scenes_.Detach();
        ReleaseVm();
        SyncViewport();
        project_.reset();
        asset_catalog_ = {};
        ++asset_catalog_revision_;
        project_lock_.Reset();
        project_hash_.clear();
        return State();
    }

    std::optional<ActionError> EditorCore::CheckLoad(const VmLoadRequest& request) const
    {
        if (const auto error = Idle(); error.has_value()) {
            return error;
        }
        if (const auto error = CheckJob({request.job_id}); error.has_value()) {
            return error;
        }

        // 同期読込みの上限を保ち、低い予算での失敗を試せるようにする。
        if (request.max_owned_bytes.has_value() &&
            (*request.max_owned_bytes == 0 || *request.max_owned_bytes > gvm_default_limits().max_owned_bytes)) {
            return ActionError{"InvalidArgument", "max_owned_bytes", "owned byte limit must be in 1..67108864"};
        }

        // 失敗後に残った旧成果物や、開き直す前の結果を今回の成功として起動しない。
        const auto& job = jobs_.at(request.job_id);
        if (project_.has_value() == false || job.project_id != project_->project_id ||
            job.input_revision != project_->revision || State().last_job != request.job_id || job.kind != "Compile" ||
            job.state != "Succeeded" || job.artifact.has_value() == false || job.artifact_hash.has_value() == false) {
            return ActionError{"InvalidState", "job_id", "compile the current project successfully before loading"};
        }
        return {};
    }

    Result<EditorState> EditorCore::LoadVm(const VmLoadRequest& request)
    {
        const auto& job = jobs_.at(request.job_id);
        const auto bytes = ReadFile(AllowedPath(*job.artifact, true), 64 * 1024 * 1024);
        if (Sha256(bytes) != *job.artifact_hash ||
            Sha256(ReadFile(ProjectPath(job.source), 1024 * 1024)) != job.source_hash ||
            Sha256(ReadFile(AllowedPath(project_->path), 64 * 1024)) != project_hash_) {
            return std::unexpected(
                ActionError{"Conflict", "job_id", "source, project or artifact changed; compile again"});
        }

        // 作成に失敗したときは現在のVMを保つ。importが必要なモジュールはこの版では未対応。
        auto limits = gvm_default_limits();
        limits.max_owned_bytes = request.max_owned_bytes.value_or(limits.max_owned_bytes);
        auto next = std::unique_ptr<gvm::Vm>{};
        try {
            next = std::make_unique<gvm::Vm>(
                std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()), gvm::Imports{}, limits);
        } catch (const gvm::Error& error) {
            return std::unexpected(ActionError{"VmError", "job_id", error.what()});
        }
        const auto id = NewId();
        const auto attached = vm_actions_.Attach(id, next->native_handle(), limits);
        if (attached.has_value() == false) {
            return std::unexpected(attached.error());
        }

        ReleaseVm();
        vm_ = std::move(next);
        vm_id_ = id;
        loaded_job_ = request.job_id;
        return State();
    }

    void EditorCore::ReleaseVm()
    {
        if (vm_id_.has_value()) {
            vm_actions_.Detach(*vm_id_);
        }
        vm_.reset();
        vm_id_.reset();
        loaded_job_.reset();
    }

    Result<CompileAccepted> EditorCore::Compile(const CompileRequest& request)
    {
        // 設定が外部で変わった場合は、古いソース一覧のまま処理せず再読込みを求める。
        if (Sha256(ReadFile(AllowedPath(project_->path), 64 * 1024)) != project_hash_) {
            return std::unexpected(
                ActionError{"Conflict", "path", "project settings changed on disk; close and reopen the project"});
        }

        // 受付時の内容とハッシュを固定し、以降のソース編集から今回の入力を切り離す。
        auto process = std::make_unique<CompileProcess>();
        process->snapshot = ReadFile(ProjectPath(request.source), 1024 * 1024);
        const auto source_hash = Sha256(process->snapshot);
        const auto id = session_id_ + "-" + std::to_string(++job_sequence_);

        // ジョブごとの一時領域と、成功時に使う出力先を用意する。
        const auto jobs = ProjectPath(".gengine/jobs", true);
        const auto build = ProjectPath(".gengine/build", true);
        std::filesystem::create_directory(jobs);
        std::filesystem::create_directory(build);
        const auto directory = ProjectPath(".gengine/jobs/" + id, true);
        if (std::filesystem::create_directory(directory) == false) {
            throw std::runtime_error("job directory already exists");
        }
        process->directory = directory;

        // 相対ソース名を保ち、バイトコードの診断位置に一時フォルダーを埋め込まない。
        process->input_argument = std::filesystem::path(Wide(request.source));
        process->input = directory / process->input_argument;
        process->output = directory / L"output.glbc";

        auto artifact = std::filesystem::path(Wide(request.source));
        artifact.replace_extension(L".glbc");
        process->destination = ProjectPath(".gengine/build/" + PathText(artifact), true);
        std::filesystem::create_directories(process->destination.parent_path());
        std::filesystem::create_directories(process->input.parent_path());
        WriteNewFile(process->input, process->snapshot);
        process->snapshot.clear();

        if (jobs_.size() >= 64) {
            // 現在のジョブがない時だけ、最も古い完了記録を外す。
            const auto oldest = std::min_element(jobs_.begin(), jobs_.end(), [](const auto& a, const auto& b) {
                return std::stoull(a.first.substr(a.first.rfind('-') + 1)) <
                       std::stoull(b.first.substr(b.first.rfind('-') + 1));
            });
            jobs_.erase(oldest);
        }

        // ここでは受付だけを確定する。子プロセスの起動と完了確認はtickに任せる。
        auto job = JobInfo{};
        job.job_id = id;
        job.project_id = request.project_id;
        job.input_revision = request.expected_revision;
        job.source = request.source;
        job.source_hash = source_hash;
        jobs_.emplace(id, std::move(job));
        active_job_ = id;
        process_ = std::move(process);
        return CompileAccepted{"Accepted", id, request.expected_revision, source_hash};
    }

    Result<JobInfo> EditorCore::Cancel(const JobRequest& request)
    {
        // 完了済みの結果は変えない。実行中なら停止を要求し、終了確認はtickで行う。
        auto& job = jobs_.at(request.job_id);
        if (job.state == "Queued" || job.state == "Running") {
            job.cancel_requested = true;
            job.phase = "Cancelling";
            if (build_) {
                build_->Cancel();
                return job;
            }
            if (process_->process.Valid()) {
                if (TerminateJobObject(process_->group.Get(), 1) == FALSE) {
                    return std::unexpected(ActionError{"IoError", "job_id", "could not request compiler termination"});
                }
            }
        }
        return job;
    }

    void EditorCore::Finish(JobInfo& job)
    {
        // 出力ができていても、取消しや制限時間超過のジョブを成功扱いにしない。
        if (job.cancel_requested) {
            job.state = "Cancelled";
        }
        else if (process_->timed_out) {
            job.state = "Failed";
            job.diagnostics.push_back({job.source, {}, {}, "compiler exceeded the 60 second deadline"});
        }
        else {
            DWORD code = 1;
            GetExitCodeProcess(process_->process.Get(), &code);
            if (code == 0) {
                // 子は一時領域だけへ出力する。成功・取消しの判定後に本体が成果物を確定する。
                const auto artifact = ReadFile(process_->output, 64 * 1024 * 1024);
                const auto destination =
                    AllowedPath(PathText(process_->destination.lexically_relative(workspace_)), true);
                if (MoveFileExW(process_->output.c_str(), destination.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
                    throw std::runtime_error("could not commit bytecode; previous artifact was preserved");
                }
                job.artifact = PathText(destination.lexically_relative(workspace_));
                job.artifact_hash = Sha256(artifact);
                job.state = "Succeeded";
            }
            else {
                // コンパイラーの診断から行・列を取り出し、元のソース名と組み合わせる。
                job.state = "Failed";
                auto match = std::smatch{};
                const auto expression = std::regex(R"(:([0-9]+):([0-9]+): ([^\r\n]+))");
                if (std::regex_search(process_->log, match, expression)) {
                    job.diagnostics.push_back({job.source, static_cast<std::uint32_t>(std::stoul(match[1])),
                                               static_cast<std::uint32_t>(std::stoul(match[2])), match[3]});
                }
                else {
                    // 位置情報のない失敗も、理由を失わず表示できる形で残す。
                    job.diagnostics.push_back(
                        {job.source,
                         {},
                         {},
                         process_->log.empty() ? "compiler failed without diagnostics" : process_->log});
                }
            }
        }
        job.phase = "Finished";
    }

    void EditorCore::Tick()
    {
        TickRunner();
        SyncViewport();
        if (active_job_.has_value() == false) {
            return;
        }

        if (build_) {
            TickBuild();
            return;
        }

        auto& job = jobs_.at(*active_job_);
        try {
            // 最初の呼出しで子を起動したら、すぐ画面のメッセージ処理へ戻る。
            if (job.state == "Queued" && job.cancel_requested == false) {
                process_->Start(compiler_);
                job.state = "Running";
                job.phase = "Compiling";
                return;
            }

            // 出力を回収し、終了済みかを待たずに調べる。実行時間には上限を設ける。
            process_->Drain();
            if (process_->process.Valid() && WaitForSingleObject(process_->process.Get(), 0) == WAIT_TIMEOUT) {
                if (std::chrono::steady_clock::now() - process_->started > std::chrono::seconds(60)) {
                    process_->timed_out = true;
                    TerminateJobObject(process_->group.Get(), 1);
                }
                return;
            }

            // 終了直前の出力も回収してから、診断と成果物を確定する。
            process_->Drain();
            Finish(job);
        } catch (const std::exception& error) {
            job.state = "Failed";
            job.phase = "Finished";
            job.diagnostics.push_back({job.source, {}, {}, std::string(error.what()).substr(0, 2048)});
        }

        // 成功・失敗のどちらでも一時領域を片付け、次の操作を受け付けられる状態へ戻す。
        process_.reset();
        active_job_.reset();
    }

    void EditorCore::Shutdown()
    {
        if (stopping_) {
            return;
        }
        preview_.reset();
        scenes_.RecoverOnDisconnect();
        scenes_.Detach();
        SyncViewport();
        play_.reset();
        play_id_.clear();
        // 新しい操作を止め、実行中の子プロセスを先に終了させる。
        stopping_ = true;
        if (runner_) {
            runner_state_.state = "Stopping";
            runner_->Stop();
            TickRunner();
        }
        if (active_job_.has_value()) {
            Cancel({*active_job_});
            // process_の破棄は子の終了を待つ。完了を偽装してから終了しない。
            process_.reset();
            build_.reset();

            auto& job = jobs_.at(*active_job_);
            job.state = "Cancelled";
            job.phase = "Finished";
            active_job_.reset();
        }

        // 子が残っていない状態でVMとプロジェクトの排他利用を解除する。
        ReleaseVm();
        project_lock_.Reset();
        project_.reset();
        asset_catalog_ = {};
        ++asset_catalog_revision_;
    }
} // namespace gengine::editor
