#pragma once
#include "asset_types.hpp"
#include "build_pipeline.hpp"
#include "build_types.hpp"
#include "performance_types.hpp"
#include "platform.hpp"
#include "play_types.hpp"
#include "play_window.hpp"
#include "scene_actions.hpp"
#include "viewport_types.hpp"
#include "vm_actions.hpp"
#include <gvm/gvm.hpp>
#include <memory>

namespace gengine::editor
{
    // ディスクに保存する設定。sources・scenes・assetsは設定ファイルからの相対パスを保持する。
    struct ProjectFile
    {
        std::uint32_t format_version = 1;
        std::string project_id;
        std::vector<std::string> sources;
        std::vector<std::string> scenes;
        std::optional<std::vector<std::string>> assets;
    };

    // 項目名とメンバーの対応から、値の変換と公開スキーマを共通で生成する。
    template <> struct Record<ProjectFile>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("format_version", &ProjectFile::format_version),
                              MakeField("project_id", &ProjectFile::project_id),
                              MakeField("sources", &ProjectFile::sources), MakeField("scenes", &ProjectFile::scenes),
                              MakeField("assets", &ProjectFile::assets)};
        }
    };

    // 開いているプロジェクトの状態。revisionは読込みごとに更新して古い操作を検出する。
    struct ProjectInfo
    {
        std::string project_id;
        std::uint64_t revision = 0;
        std::string path;
        std::vector<std::string> sources;
        std::vector<std::string> scenes;
        bool dirty = false;
        std::vector<std::string> assets;
    };

    template <> struct Record<ProjectInfo>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("project_id", &ProjectInfo::project_id),
                              MakeField("revision", &ProjectInfo::revision),
                              MakeField("path", &ProjectInfo::path),
                              MakeField("sources", &ProjectInfo::sources),
                              MakeField("scenes", &ProjectInfo::scenes),
                              MakeField("dirty", &ProjectInfo::dirty),
                              MakeField("assets", &ProjectInfo::assets)};
        }
    };

    // GUIの再描画とMCPの状態照会が参照する情報。未オープン・ジョブなしはnullで表す。
    struct EditorState
    {
        std::string session_id;
        std::optional<ProjectInfo> project;
        std::optional<std::string> active_job;
        std::optional<std::string> last_job;
        std::uint32_t job_limit = 64;
        std::optional<std::string> vm_id;
        std::optional<std::string> loaded_job;
        std::optional<DocumentInfo> document;
    };

    template <> struct Record<EditorState>
    {
        static auto Fields()
        {
            return std::tuple{
                MakeField("session_id", &EditorState::session_id), MakeField("project", &EditorState::project),
                MakeField("active_job", &EditorState::active_job), MakeField("last_job", &EditorState::last_job),
                MakeField("job_limit", &EditorState::job_limit),   MakeField("vm_id", &EditorState::vm_id),
                MakeField("loaded_job", &EditorState::loaded_job), MakeField("document", &EditorState::document)};
        }
    };

    // 通常はworkspace基準。自動選択を有効にしたGUIの読込みだけ絶対パスも受け付ける。
    struct ProjectPathRequest
    {
        std::string path;
    };

    template <> struct Record<ProjectPathRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("path", &ProjectPathRequest::path)};
        }
    };

    struct ProjectCreateRequest
    {
        std::string path;
        std::optional<std::string> template_name;
    };
    template <> struct Record<ProjectCreateRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("path", &ProjectCreateRequest::path),
                              MakeField("template_name", &ProjectCreateRequest::template_name)};
        }
    };

    // 要求側が見ていたプロジェクトと版が一致するときだけ閉じる。
    struct CloseRequest
    {
        std::string project_id;
        std::uint64_t expected_revision = 0;
        std::optional<std::string> choice;
        std::optional<std::uint64_t> document_revision;
    };

    template <> struct Record<CloseRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("project_id", &CloseRequest::project_id),
                              MakeField("expected_revision", &CloseRequest::expected_revision),
                              MakeField("choice", &CloseRequest::choice),
                              MakeField("document_revision", &CloseRequest::document_revision)};
        }
    };

    // 登録済みのソースを一件指定する。sourceはプロジェクト設定からの相対パス。
    struct CompileRequest
    {
        std::string project_id;
        std::uint64_t expected_revision = 0;
        std::string source;
    };

    template <> struct Record<CompileRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("project_id", &CompileRequest::project_id),
                              MakeField("expected_revision", &CompileRequest::expected_revision),
                              MakeField("source", &CompileRequest::source)};
        }
    };

    struct JobRequest
    {
        std::string job_id;
    };

    template <> struct Record<JobRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("job_id", &JobRequest::job_id)};
        }
    };

    // ファイル操作など位置を特定できない失敗では、行・列を持たず理由だけを返す。
    struct Diagnostic
    {
        std::string source;
        std::optional<std::uint32_t> line;
        std::optional<std::uint32_t> column;
        std::string message;
    };

    template <> struct Record<Diagnostic>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("source", &Diagnostic::source), MakeField("line", &Diagnostic::line),
                              MakeField("column", &Diagnostic::column), MakeField("message", &Diagnostic::message)};
        }
    };

    // コンパイルの受付結果。成功通知ではなく、以後はjob_idで完了を照会する。
    struct CompileAccepted
    {
        std::string status = "Accepted";
        std::string job_id;
        std::uint64_t input_revision = 0;
        std::string source_hash;
    };

    template <> struct Record<CompileAccepted>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("status", &CompileAccepted::status),
                              MakeField("job_id", &CompileAccepted::job_id),
                              MakeField("input_revision", &CompileAccepted::input_revision),
                              MakeField("source_hash", &CompileAccepted::source_hash)};
        }
    };

    // 入力の識別情報と進行状態を保持し、成功したジョブだけに成果物を付ける。
    struct JobInfo
    {
        std::string job_id;
        std::string project_id;
        std::uint64_t input_revision = 0;
        std::string source;
        std::string source_hash;

        std::string state = "Queued";
        std::string phase = "Waiting";
        bool cancel_requested = false;

        std::vector<Diagnostic> diagnostics;
        std::optional<std::string> artifact;
        std::optional<std::string> artifact_hash;
        std::string kind = "Compile";
        std::optional<std::string> scene;
        std::optional<std::string> game;
        std::optional<std::uint64_t> document_revision;
        std::optional<std::string> input_hash;
    };

    template <> struct Record<JobInfo>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("job_id", &JobInfo::job_id),
                              MakeField("project_id", &JobInfo::project_id),
                              MakeField("input_revision", &JobInfo::input_revision),
                              MakeField("source", &JobInfo::source),
                              MakeField("source_hash", &JobInfo::source_hash),
                              MakeField("state", &JobInfo::state),
                              MakeField("phase", &JobInfo::phase),
                              MakeField("cancel_requested", &JobInfo::cancel_requested),
                              MakeField("diagnostics", &JobInfo::diagnostics),
                              MakeField("artifact", &JobInfo::artifact),
                              MakeField("artifact_hash", &JobInfo::artifact_hash),
                              MakeField("kind", &JobInfo::kind),
                              MakeField("scene", &JobInfo::scene),
                              MakeField("game", &JobInfo::game),
                              MakeField("document_revision", &JobInfo::document_revision),
                              MakeField("input_hash", &JobInfo::input_hash)};
        }
    };

    // 文書とジョブの状態はGUIの所有スレッドだけで変更する。
    class EditorCore
    {
      public:
        // 自動選択は通常GUI専用。MCPや明示的なworkspaceでは既定の固定範囲を使う。
        EditorCore(std::filesystem::path workspace, std::filesystem::path compiler, bool automatic_workspace = false);
        ~EditorCore();

        EditorCore(const EditorCore&) = delete;
        EditorCore& operator=(const EditorCore&) = delete;

        // 明示的に再検証した小さな結果だけをGUIとMCPで共有する。
        const AssetCatalog& asset_catalog() const
        {
            return asset_catalog_;
        }
        std::uint64_t asset_catalog_revision() const
        {
            return asset_catalog_revision_;
        }

        SceneActions& scenes()
        {
            return scenes_;
        }

        Dispatcher& dispatcher()
        {
            return dispatcher_;
        }

        // GUIのタイマーから呼び、子の終了を待たずにジョブを一段階進める。
        void Tick();

        // 新規操作を止め、子の終了と一時領域の後始末を済ませてプロジェクトを解放する。
        void Shutdown();
        // 親HWNDはGUIが所有する。試遊の寿命は共通コアが管理する。
        void SetPlayParent(HWND parent)
        {
            play_parent_ = parent;
        }
        HWND PlayHandle() const
        {
            return play_ != nullptr ? play_->window() : nullptr;
        }
        PlayState PlayStatus() const;
        void RecordHostTime(Timing timing, std::uint64_t ns)
        {
            if (play_) {
                play_->RecordHostTime(timing, ns);
            }
        }
        const RunnerState& RunnerStatus() const
        {
            return runner_state_;
        }
        bool RunnerActive() const
        {
            return runner_ != nullptr;
        }
        void UpdatePreview();
        ViewportState ViewportStatus();
        void SelectViewportObject(std::string id);
        void BeginPlacement(std::string kind);
        void PointerDown(Point2d pixel, double width, double height, bool pan, bool orbit = false);
        void PointerMove(Point2d pixel);
        void PointerUp();
        void CancelViewportGesture();
        void ZoomViewport(Point2d pixel, double width, double height, double factor);
        void DeleteViewportObject();
        void SyncViewport();
        std::optional<std::vector<double>> ViewportCandidate() const;
        HWND SceneViewHandle() const
        {
            return play_ ? play_->window() : preview_ ? preview_->window() : nullptr;
        }
        std::vector<std::uint8_t> TakeCapture()
        {
            return std::exchange(capture_, {});
        }

        const std::filesystem::path& workspace() const
        {
            return workspace_;
        }

      private:
        struct CompileProcess;

        // 操作定義と実行可否の確認を、GUI・MCPの両方から使う。
        Registry Definitions();
        void RegisterAssets(Registry& registry);
        void RegisterBuild(Registry& registry);
        std::optional<ActionError> CheckBuild(const BuildStart& request) const;
        Result<CompileAccepted> StartBuild(const BuildStart& request);
        void TickBuild();
        void TickRunner();
        AssetCatalog ListAssets() const;
        void RegisterPlay(Registry& registry);
        void RegisterViewport(Registry& registry);
        void DrawViewport();
        void ApplyViewportEdit(std::string_view action, Value value);
        void PointerDown3d(Point2d pixel, double width, double height, bool pan, bool orbit);
        void PointerMove3d(Point2d pixel);
        void PointerUp3d();
        void ZoomViewport3d(Point2d pixel, double width, double height, double factor);
        EditorState State() const;
        std::optional<ActionError> Idle() const;
        std::optional<ActionError> CheckProject(std::string_view id, std::uint64_t revision) const;
        std::optional<ActionError> CheckCompile(const CompileRequest& request) const;
        std::optional<ActionError> CheckJob(const JobRequest& request) const;

        // プロジェクトの操作と、受付後に継続するコンパイルの状態遷移。
        Result<EditorState> CreateProject(const ProjectCreateRequest& request);
        Result<EditorState> OpenProject(const ProjectPathRequest& request);
        Result<EditorState> ReadProject(const ProjectPathRequest& request);
        Result<EditorState> CloseProject(const CloseRequest& request);
        Result<CompileAccepted> Compile(const CompileRequest& request);
        Result<JobInfo> Cancel(const JobRequest& request);
        void Finish(JobInfo& job);

        // 成果物を検証してからVMを入れ替える。破棄前に操作対象の登録を解除する。
        std::optional<ActionError> CheckLoad(const VmLoadRequest& request) const;
        Result<EditorState> LoadVm(const VmLoadRequest& request);
        void ReleaseVm();

        // ユーザーの参照は予約領域を除外し、Editor自身の作業用パスだけinternalで許可する。
        std::filesystem::path AllowedPath(std::string_view relative, bool internal = false) const;
        std::filesystem::path ProjectPath(std::string_view relative, bool internal = false) const;

        std::filesystem::path workspace_;
        std::filesystem::path compiler_;
        bool automatic_workspace_ = false;
        std::string session_id_;

        AssetCatalog asset_catalog_;
        std::uint64_t asset_catalog_revision_ = 0;
        std::optional<ProjectInfo> project_;
        std::string project_hash_;
        std::uint64_t revision_ = 0;
        std::uint64_t job_sequence_ = 0;
        bool stopping_ = false;
        Handle project_lock_;

        std::map<std::string, JobInfo, std::less<>> jobs_;
        std::optional<std::string> active_job_;
        std::unique_ptr<CompileProcess> process_;
        std::unique_ptr<BuildPipeline> build_;
        std::unique_ptr<ChildProcess> runner_;
        RunnerState runner_state_;
        VmActions vm_actions_;
        std::unique_ptr<gvm::Vm> vm_;
        std::optional<std::string> vm_id_;
        std::optional<std::string> loaded_job_;
        SceneActions scenes_;
        Dispatcher dispatcher_;
        HWND play_parent_ = nullptr;
        std::unique_ptr<gengine::PlayWindow> play_;
        std::string play_id_;
        std::unique_ptr<gengine::PlayWindow> preview_;
        std::uint64_t preview_revision_ = 0;
        bool preview_assets_dirty_ = false;
        std::string preview_game_;
        std::string viewport_document_;
        std::string viewport_path_;
        std::uint64_t viewport_revision_ = 0;
        View2d viewport_view_;
        View3d viewport_view_3d_;
        std::string viewport_selected_;
        std::string placement_kind_;
        std::optional<ViewportGesture> viewport_gesture_;
        std::optional<ViewportGesture3d> viewport_gesture_3d_;
        std::uint64_t play_revision_ = 0;
        std::optional<std::string> play_document_;
        std::vector<std::uint8_t> capture_;
    };
} // namespace gengine::editor
