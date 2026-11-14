#include "editor_core.hpp"

namespace gengine::editor
{
    PlayState EditorCore::PlayStatus() const
    {
        if (play_ == nullptr) {
            return {"", "Stopped"};
        }
        const auto& state = play_->game().state();
        const auto names = std::array{"Start", "Playing", "Result", "Faulted"};
        return {play_id_,
                names[static_cast<std::size_t>(state.phase)],
                state.paused,
                state.preset,
                state.tick,
                state.outcome,
                state.diagnostic,
                play_revision_,
                state.owned_bytes,
                state.update_ns,
                state.contact_ns,
                state.apply_ns,
                state.dropped_shots,
                static_cast<std::uint64_t>(state.dropped_seconds * 1000000),
                play_->game().mode() == GameMode::k3d ? "3d" : "2d",
                play_document_};
    }

    void EditorCore::UpdatePreview()
    {
        SyncViewport();
        if (play_ || scenes_.document() == nullptr || play_parent_ == nullptr) {
            preview_.reset();
            return;
        }
        const auto revision = scenes_.Revision();
        if (preview_ && preview_revision_ == revision && preview_assets_dirty_ == false) {
            return;
        }
        const auto& scene = *scenes_.document();
        const auto references = AssetSource(AllowedPath(project_->path).parent_path());
        // 同じ次元ではHWNDと描画資源を保ち、編集内容だけを差し替える。
        if (preview_ && preview_game_ == scene.game) {
            preview_->UpdatePreviewAssets(scene, references, preview_assets_dirty_);
            preview_assets_dirty_ = false;
            preview_revision_ = revision;
            DrawViewport();
            return;
        }
        auto next = std::make_unique<gengine::PlayWindow>(
            play_parent_, std::span<const std::uint8_t>{}, compiler_.parent_path() / "backends",
            scene.game == "3d" ? GameMode::k3d : GameMode::k2d, scene, true, nullptr, &references);
        preview_ = std::move(next);
        preview_assets_dirty_ = false;
        preview_revision_ = revision;
        preview_game_ = scene.game;
        DrawViewport();
    }

    void EditorCore::RegisterPlay(Registry& registry)
    {
        registry.Add<PlayStart, PlayState>(
            "play.start", "最新成果物を埋め込みPIEで実行する", "runtime-control", true,
            [this](const PlayStart& request) -> std::optional<ActionError> {
                if (request.preset > 31) {
                    return ActionError{"InvalidArgument", "preset", "preset must be 0..31 and declared by game"};
                }
                if (request.game.has_value() && *request.game != "2d" && *request.game != "3d") {
                    return ActionError{"InvalidArgument", "game", "game must be 2d or 3d"};
                }
                if (play_parent_ == nullptr) {
                    return ActionError{"InvalidState", {}, "preview parent is unavailable"};
                }
                if (const auto* scene = scenes_.document()) {
                    if (request.document_id.has_value() == false || request.expected_revision.has_value() == false) {
                        return ActionError{"InvalidArgument", "document_id",
                                           "open scene requires document_id and expected_revision"};
                    }
                    if (const auto error = scenes_.Check(*request.document_id, *request.expected_revision); error) {
                        return error;
                    }
                    if (request.game && *request.game != scene->game) {
                        return ActionError{"InvalidArgument", "game", "scene dimension differs"};
                    }
                    try {
                        ValidateSceneForPlay(*scene);
                    } catch (const std::exception& e) {
                        return ActionError{"InvalidState", "scene", e.what()};
                    }
                }
                else if (request.document_id || request.expected_revision) {
                    return ActionError{"NotFound", "document_id", "no scene is open"};
                }
                return CheckLoad({request.job_id, {}});
            },
            [this](const PlayStart& request) -> Result<PlayState> {
                try {
                    const auto& job = jobs_.at(request.job_id);
                    const auto bytes = ReadFile(AllowedPath(*job.artifact, true), 64 * 1024 * 1024);
                    if (Sha256(bytes) != *job.artifact_hash ||
                        Sha256(ReadFile(ProjectPath(job.source), 1024 * 1024)) != job.source_hash ||
                        Sha256(ReadFile(AllowedPath(project_->path), 64 * 1024)) != project_hash_) {
                        return std::unexpected(ActionError{"Conflict", "job_id", "compile changed inputs again"});
                    }
                    const auto* scene = scenes_.document();
                    const auto mode = scene ? scene->game : request.game.value_or("2d");
                    const auto references = AssetSource(AllowedPath(project_->path).parent_path());
                    auto next = std::make_unique<gengine::PlayWindow>(
                        play_parent_, std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()),
                        compiler_.parent_path() / "backends", mode == "3d" ? GameMode::k3d : GameMode::k2d,
                        scene ? std::optional(*scene) : std::nullopt, false, nullptr, &references);
                    next->Reset(request.preset);
                    CancelViewportGesture();
                    preview_.reset();
                    play_revision_ = scene ? scenes_.State().revision : project_->revision;
                    play_document_ = scene ? std::optional(scene->document_id) : std::nullopt;
                    play_ = std::move(next);
                    play_id_ = NewId();
                    return PlayStatus();
                } catch (const std::exception& error) {
                    return std::unexpected(ActionError{"RuntimeError", {}, error.what()});
                }
            });
        const auto check = [this](const PlayRequest& request) -> std::optional<ActionError> {
            if (play_ == nullptr || play_id_ != request.play_id) {
                return ActionError{"NotFound", "play_id", "play session is not active"};
            }
            return {};
        };
        registry.Add<CaptureRequest, PlayState>(
            "viewport.capture", "試遊または編集ビューのPNGを取得する", "read", false,
            [this](const CaptureRequest& r) -> std::optional<ActionError> {
                if (r.play_id.has_value() == r.document_id.has_value()) {
                    return ActionError{"InvalidArgument", "play_id", "choose one play_id or document_id"};
                }
                if (r.play_id) {
                    if (play_ == nullptr || *r.play_id != play_id_) {
                        return ActionError{"NotFound", "play_id", "play is not active"};
                    }
                    return {};
                }
                if (r.expected_revision.has_value() == false) {
                    return ActionError{"InvalidArgument", "expected_revision", "revision required"};
                }
                if (ViewportStatus().transient) {
                    return ActionError{"Busy", "document_id", "finish or cancel the viewport gesture before capture"};
                }
                return scenes_.Check(*r.document_id, *r.expected_revision);
            },
            [this](const CaptureRequest& r) -> Result<PlayState> {
                try {
                    capture_.clear();
                    const auto capture = [this](PlayWindow& window) {
                        auto image = window.Capture();
                        if (image.size() > 512 * 1024) {
                            throw std::runtime_error("capture exceeds transport budget");
                        }
                        capture_ = std::move(image);
                    };
                    if (r.play_id) {
                        capture(*play_);
                        return PlayStatus();
                    }
                    UpdatePreview();
                    if (preview_ == nullptr) {
                        return std::unexpected(
                            ActionError{"InvalidState", "document_id", "preview parent unavailable"});
                    }
                    capture(*preview_);
                    auto state = PlayState{};
                    state.phase = "Editing";
                    state.input_revision = scenes_.State().revision;
                    state.document_id = r.document_id;
                    state.game = scenes_.document()->game;
                    return state;
                } catch (const std::exception& e) {
                    return std::unexpected(ActionError{"CaptureError", {}, e.what()});
                }
            });
        registry.Add<PlayRequest, PlayState>("play.get_state", "試遊状態を取得する", "read", false, check,
                                             [this](const PlayRequest&) -> Result<PlayState> {
                                                 return PlayStatus();
                                             });
        registry.Add<PerformanceStart, PerformanceReport>(
            "performance.start", "PIEのウォームアップと上限付き計測を開始する", "runtime-control", true,
            [this, check](const PerformanceStart& request) -> std::optional<ActionError> {
                if (const auto error = check(PlayRequest{request.play_id}); error) {
                    return error;
                }
                if (play_->game().state().phase == Phase::kFaulted) {
                    return ActionError{"InvalidState", "play_id", "reset faulted play before capture"};
                }
                if (play_->PerformanceActive()) {
                    return ActionError{"Busy", "play_id", "stop the current capture first"};
                }
                if (request.config.warmup > 600 || request.config.samples == 0 || request.config.samples > 3600) {
                    return ActionError{"InvalidArgument", "config", "warmup 0..600; samples 1..3600"};
                }
                return {};
            },
            [this](const PerformanceStart& request) -> Result<PerformanceReport> {
                play_->StartPerformance(request.config);
                return play_->Performance();
            });
        registry.Add<PlayRequest, PerformanceReport>("performance.get", "PIEの計測分布・資源・プロセス使用量を取得する",
                                                     "read", false, check,
                                                     [this](const PlayRequest&) -> Result<PerformanceReport> {
                                                         return play_->Performance();
                                                     });
        registry.Add<PlayRequest, PerformanceReport>("performance.stop", "計測を中止し収集済みの標本を保持する",
                                                     "runtime-control", true, check,
                                                     [this](const PlayRequest&) -> Result<PerformanceReport> {
                                                         play_->StopPerformance();
                                                         return play_->Performance();
                                                     });
        registry.Add<PlayRequest, PlayState>("play.stop", "試遊を停止して編集へ戻る", "runtime-control", true, check,
                                             [this](const PlayRequest&) -> Result<PlayState> {
                                                 play_.reset();
                                                 play_id_.clear();
                                                 return PlayStatus();
                                             });
        registry.Add<PlayRequest, PlayState>(
            "play.pause", "試遊と音声の一時停止状態を指定する", "runtime-control", true,
            [check](const PlayRequest& request) -> std::optional<ActionError> {
                if (request.paused.has_value() == false) {
                    return ActionError{"InvalidArgument", "paused", "explicit pause state required"};
                }
                return check(request);
            },
            [this](const PlayRequest& request) -> Result<PlayState> {
                try {
                    play_->Pause(*request.paused);
                    return PlayStatus();
                } catch (const std::exception& error) {
                    return std::unexpected(ActionError{"RuntimeError", {}, error.what()});
                }
            });
        registry.Add<PlayRequest, PlayState>(
            "play.reset", "同条件でリセット、または通常開始へ戻る", "runtime-control", true, check,
            [this](const PlayRequest& request) -> Result<PlayState> {
                try {
                    play_->Reset(request.normal.value_or(false) ? 0 : play_->game().state().preset);
                    return PlayStatus();
                } catch (const std::exception& error) {
                    return std::unexpected(ActionError{"RuntimeError", {}, error.what()});
                }
            });
    }
} // namespace gengine::editor
