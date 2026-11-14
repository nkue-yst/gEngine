#include "editor_core.hpp"
#include <algorithm>
#include <cmath>

namespace gengine::editor
{
    void EditorCore::SyncViewport()
    {
        const auto* scene = scenes_.document();
        if (scene == nullptr) {
            viewport_document_.clear();
            viewport_path_.clear();
            viewport_selected_.clear();
            viewport_gesture_.reset();
            viewport_gesture_3d_.reset();
            placement_kind_.clear();
            return;
        }
        if (viewport_document_ != scene->document_id || viewport_path_ != scenes_.Path()) {
            viewport_gesture_.reset();
            viewport_gesture_3d_.reset();
            placement_kind_.clear();
            if (scene->game == "3d") {
                viewport_view_3d_ = FitScene3d(*scene);
            }
            else {
                viewport_view_ = FitScene2d(*scene);
            }
            viewport_selected_.clear();
            viewport_document_ = scene->document_id;
            viewport_path_ = scenes_.Path();
        }
        // 外部編集・履歴操作・再読込みの後に古い仮位置を確定させない。
        if (viewport_revision_ != scenes_.Revision() || play_ || RunnerActive() || build_) {
            if (viewport_gesture_ && viewport_gesture_->pan) {
                viewport_view_ = viewport_gesture_->mapping.view;
            }
            if (viewport_gesture_3d_ && viewport_gesture_3d_->kind != Gesture3dKind::kAxis) {
                viewport_view_3d_ = viewport_gesture_3d_->mapping.view;
            }
            viewport_gesture_.reset();
            viewport_gesture_3d_.reset();
            placement_kind_.clear();
            viewport_revision_ = scenes_.Revision();
        }
        if (std::none_of(scene->objects.begin(), scene->objects.end(), [&](const auto& object) {
                return object.id == viewport_selected_;
            })) {
            viewport_selected_.clear();
        }
    }

    ViewportState EditorCore::ViewportStatus()
    {
        SyncViewport();
        if (scenes_.document() && scenes_.document()->game == "3d") {
            return {viewport_document_,
                    viewport_revision_,
                    {viewport_view_3d_.center.begin(), viewport_view_3d_.center.end()},
                    viewport_view_3d_.height,
                    viewport_selected_,
                    viewport_gesture_3d_.has_value() || placement_kind_.empty() == false,
                    viewport_view_3d_.yaw_degrees,
                    viewport_view_3d_.pitch_degrees};
        }
        return {viewport_document_,
                viewport_revision_,
                {viewport_view_.center[0], viewport_view_.center[1]},
                viewport_view_.height,
                viewport_selected_,
                viewport_gesture_.has_value() || placement_kind_.empty() == false};
    }

    std::optional<std::vector<double>> EditorCore::ViewportCandidate() const
    {
        if (viewport_gesture_3d_ && viewport_gesture_3d_->kind == Gesture3dKind::kAxis) {
            const auto& point = viewport_gesture_3d_->candidate;
            return std::vector<double>(point.begin(), point.end());
        }
        if (viewport_gesture_ && viewport_gesture_->pan == false) {
            const auto& point = viewport_gesture_->candidate;
            return std::vector<double>(point.begin(), point.end());
        }
        return {};
    }

    void EditorCore::DrawViewport()
    {
        if (preview_ == nullptr || viewport_document_.empty() || play_) {
            return;
        }
        auto scene = *scenes_.document();
        const auto candidate = ViewportCandidate();
        if (candidate) {
            for (auto& object : scene.objects) {
                if (object.id == viewport_selected_) {
                    for (auto axis = 0u; axis < candidate->size(); ++axis) {
                        object.position[axis] = float((*candidate)[axis]);
                    }
                }
            }
        }
        if (scene.game == "3d") {
            preview_->UpdatePreviewScene3d(scene, viewport_view_3d_, viewport_selected_);
        }
        else {
            preview_->UpdatePreviewScene(scene, viewport_view_, viewport_selected_);
        }
    }

    void EditorCore::SelectViewportObject(std::string id)
    {
        CancelViewportGesture();
        SyncViewport();
        const auto* scene = scenes_.document();
        if (scene == nullptr || play_) {
            return;
        }
        if (id.empty() == false && std::none_of(scene->objects.begin(), scene->objects.end(), [&](const auto& o) {
                return o.id == id;
            })) {
            throw std::invalid_argument("object does not exist");
        }
        viewport_selected_ = std::move(id);
        DrawViewport();
    }

    void EditorCore::CancelViewportGesture()
    {
        if (viewport_gesture_ && viewport_gesture_->pan) {
            viewport_view_ = viewport_gesture_->mapping.view;
        }
        if (viewport_gesture_3d_ && viewport_gesture_3d_->kind != Gesture3dKind::kAxis) {
            viewport_view_3d_ = viewport_gesture_3d_->mapping.view;
        }
        viewport_gesture_.reset();
        viewport_gesture_3d_.reset();
        placement_kind_.clear();
        SyncViewport();
        DrawViewport();
    }

    void EditorCore::BeginPlacement(std::string kind)
    {
        CancelViewportGesture();
        const auto* scene = scenes_.document();
        if (scene == nullptr || play_) {
            throw std::logic_error("placement requires a stopped scene");
        }
        if (kind != "player_spawn" && kind != "enemy_spawn") {
            throw std::invalid_argument("choose a player or enemy spawn");
        }
        if (std::any_of(scene->objects.begin(), scene->objects.end(), [&](const auto& o) {
                return o.kind == kind;
            })) {
            throw std::invalid_argument("this object kind already exists");
        }
        placement_kind_ = std::move(kind);
    }

    void EditorCore::ApplyViewportEdit(std::string_view action, Value value)
    {
        // マウスも既存のDispatcherへ通し、検証・履歴・再送の規則を共用する。
        const auto submitted = dispatcher_.Submit(Request{std::string(action), 1, NewId(), std::move(value)});
        if (submitted.has_value() == false) {
            throw std::runtime_error(submitted.error().reason);
        }
        dispatcher_.Pump();
        const auto operation = dispatcher_.Lookup(submitted->operation_id).operation;
        if (operation->error) {
            throw std::runtime_error(operation->error->reason);
        }
        if (action == "scene.create_object") {
            const auto result = Decode<DocumentInfo>(operation->result).value();
            if (result.changed_ids.empty() == false) {
                viewport_selected_ = result.changed_ids.front();
            }
        }
    }

    void EditorCore::PointerDown(Point2d pixel, double width, double height, bool pan, bool orbit)
    {
        SyncViewport();
        if (viewport_document_.empty() || play_) {
            return;
        }
        if (scenes_.document()->game == "3d") {
            PointerDown3d(pixel, width, height, pan, orbit);
            return;
        }
        if (orbit) {
            return;
        }
        const auto mapping = MakeMapping2d(viewport_view_, width, height);
        if (mapping.Contains(pixel) == false) {
            return;
        }
        const auto& scene = *scenes_.document();
        const auto world = mapping.ToWorld(pixel);
        // 追加待ちのクリックは位置を明示した一件の作成要求へ変換する。
        if (placement_kind_.empty() == false && pan == false) {
            if (world[0] < scene.settings.bounds_min[0] || world[0] > scene.settings.bounds_max[0] ||
                world[1] < scene.settings.bounds_min[1] || world[1] > scene.settings.bounds_max[1]) {
                throw std::invalid_argument("choose a position inside the movement bounds");
            }
            const auto kind = std::exchange(placement_kind_, {});
            ApplyViewportEdit("scene.create_object",
                              Encode(SceneCreate{viewport_document_, viewport_revision_, kind,
                                                 Value::Object{{"position", Value::Array{world[0], world[1]}}}}));
            SyncViewport();
            UpdatePreview();
            return;
        }
        if (pan) {
            viewport_gesture_ = ViewportGesture{mapping, pixel, {}, {}, Handle2d::kNone, true, viewport_revision_};
            return;
        }
        for (const auto& object : scene.objects) {
            if (object.id == viewport_selected_ && object.kind != "game_camera") {
                const auto position = Point2d{object.position[0], object.position[1]};
                const auto center = mapping.ToPixel(position);
                const auto handle = HitHandle2d(center, pixel, HandleDirection2d(mapping, center));
                if (handle != Handle2d::kNone) {
                    viewport_gesture_ =
                        ViewportGesture{mapping, pixel, position, position, handle, false, viewport_revision_};
                    return;
                }
            }
        }
        viewport_selected_ = PickScene2d(scene, mapping, pixel);
        DrawViewport();
    }

    void EditorCore::PointerMove(Point2d pixel)
    {
        SyncViewport();
        if (viewport_gesture_3d_) {
            PointerMove3d(pixel);
            return;
        }
        if (viewport_gesture_.has_value() == false) {
            return;
        }
        auto& drag = *viewport_gesture_;
        const auto start = drag.mapping.ToWorld(drag.start);
        const auto current = drag.mapping.ToWorld(pixel);
        const auto& settings = scenes_.document()->settings;
        // 開始時の変換から差分を求め、操作中の軸だけを有効範囲へ収める。
        for (auto axis = 0u; axis < 2; ++axis) {
            if (drag.pan) {
                viewport_view_.center[axis] = drag.mapping.view.center[axis] + start[axis] - current[axis];
            }
            else if (drag.handle == Handle2d::kPlane || (axis == 0 && drag.handle == Handle2d::kX) ||
                     (axis == 1 && drag.handle == Handle2d::kY)) {
                drag.candidate[axis] = std::clamp(drag.original[axis] + current[axis] - start[axis],
                                                  double(settings.bounds_min[axis]), double(settings.bounds_max[axis]));
            }
        }
        DrawViewport();
    }

    void EditorCore::PointerUp()
    {
        SyncViewport();
        if (viewport_gesture_3d_) {
            PointerUp3d();
            return;
        }
        if (viewport_gesture_.has_value() == false) {
            return;
        }
        const auto drag = *viewport_gesture_;
        viewport_gesture_.reset();
        // 仮表示を終えてから確定する。変更なしも共通履歴の判定に任せる。
        try {
            if (drag.pan) {
                ApplyViewportEdit("viewport.set_view",
                                  Encode(ViewportSet{viewport_document_,
                                                     drag.revision,
                                                     {viewport_view_.center[0], viewport_view_.center[1]},
                                                     viewport_view_.height}));
            }
            else {
                ApplyViewportEdit("scene.set_properties",
                                  Encode(SceneSet{viewport_document_,
                                                  drag.revision,
                                                  viewport_selected_,
                                                  {{"position", Value::Array{drag.candidate[0], drag.candidate[1]}}}}));
            }
        } catch (...) {
            DrawViewport();
            throw;
        }
        UpdatePreview();
        DrawViewport();
    }

    void EditorCore::ZoomViewport(Point2d pixel, double width, double height, double factor)
    {
        CancelViewportGesture();
        if (viewport_document_.empty() || play_) {
            return;
        }
        if (scenes_.document()->game == "3d") {
            ZoomViewport3d(pixel, width, height, factor);
            return;
        }
        const auto mapping = MakeMapping2d(viewport_view_, width, height);
        if (mapping.Contains(pixel) == false) {
            return;
        }
        auto view = ZoomView2d(mapping, pixel, factor);
        const auto base = FitScene2d(*scenes_.document()).height;
        view = ZoomView2d(mapping, pixel, std::clamp(view.height, base / 100, base * 100) / viewport_view_.height);
        ApplyViewportEdit(
            "viewport.set_view",
            Encode(ViewportSet{viewport_document_, viewport_revision_, {view.center[0], view.center[1]}, view.height}));
    }

    void EditorCore::DeleteViewportObject()
    {
        CancelViewportGesture();
        if (viewport_selected_.empty() == false && play_ == nullptr) {
            ApplyViewportEdit("scene.delete_objects",
                              Encode(SceneDelete{viewport_document_, viewport_revision_, {viewport_selected_}}));
            SyncViewport();
            UpdatePreview();
        }
    }

    void EditorCore::RegisterViewport(Registry& registry)
    {
        const auto check = [this](const auto& request) -> std::optional<ActionError> {
            if (const auto error = scenes_.Check(request.document_id, request.expected_revision)) {
                return error;
            }
            return {};
        };
        registry.Add<DocumentRequest, ViewportState>("viewport.get_state",
                                                     "編集ビューの中心・高さ・3Dの向きと選択を取得する", "read", false,
                                                     check, [this](const auto&) -> Result<ViewportState> {
                                                         return ViewportStatus();
                                                     });
        registry.Add<ViewportSet, ViewportState>(
            "viewport.set_view", "編集ビューの中心XY/XYZ・表示高さ・3Dのyaw/pitchを指定する", "view-control", true,
            [this, check](const ViewportSet& r) -> std::optional<ActionError> {
                if (const auto error = check(r)) {
                    return error;
                }
                const auto is_3d = scenes_.document()->game == "3d";
                if (r.center.size() != (is_3d ? 3u : 2u) ||
                    std::any_of(r.center.begin(), r.center.end(),
                                [](double v) {
                                    return std::isfinite(v) == false || std::abs(v) > 1e30;
                                }) ||
                    std::isfinite(r.height) == false || r.height < 1e-6 || r.height > 1e30) {
                    return ActionError{"InvalidArgument", "center/height",
                                       "finite center XY (2D) / XYZ (3D) within +/-1e30; height 1e-6..1e30 required"};
                }
                if ((is_3d == false && (r.yaw_degrees || r.pitch_degrees)) ||
                    (r.yaw_degrees && (std::isfinite(*r.yaw_degrees) == false || std::abs(*r.yaw_degrees) > 1e6)) ||
                    (r.pitch_degrees &&
                     (std::isfinite(*r.pitch_degrees) == false || std::abs(*r.pitch_degrees) > 85))) {
                    return ActionError{"InvalidArgument", "yaw_degrees/pitch_degrees",
                                       "3D only: yaw within +/-1e6, pitch -85..85"};
                }
                return {};
            },
            [this](const ViewportSet& r) -> Result<ViewportState> {
                CancelViewportGesture();
                if (scenes_.document()->game == "3d") {
                    viewport_view_3d_ = {{r.center[0], r.center[1], r.center[2]},
                                         r.height,
                                         r.yaw_degrees.value_or(viewport_view_3d_.yaw_degrees),
                                         r.pitch_degrees.value_or(viewport_view_3d_.pitch_degrees)};
                }
                else {
                    viewport_view_ = {{r.center[0], r.center[1]}, r.height};
                }
                DrawViewport();
                return ViewportStatus();
            });
        registry.Add<DocumentRequest, ViewportState>("viewport.reset_view", "移動範囲とカメラ枠を全体表示する",
                                                     "view-control", true, check,
                                                     [this](const auto&) -> Result<ViewportState> {
                                                         CancelViewportGesture();
                                                         if (scenes_.document()->game == "3d") {
                                                             viewport_view_3d_ = FitScene3d(*scenes_.document());
                                                         }
                                                         else {
                                                             viewport_view_ = FitScene2d(*scenes_.document());
                                                         }
                                                         DrawViewport();
                                                         return ViewportStatus();
                                                     });
    }
} // namespace gengine::editor
