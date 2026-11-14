#include "editor_core.hpp"
#include "picking3d.hpp"
#include <algorithm>
#include <cmath>

namespace gengine::editor
{
    void EditorCore::PointerDown3d(Point2d pixel, double width, double height, bool pan, bool orbit)
    {
        const auto mapping = MakeMapping3d(viewport_view_3d_, width, height);
        if (mapping.area.Contains(pixel) == false) {
            return;
        }
        const auto& scene = *scenes_.document();
        if (placement_kind_.empty() == false && pan == false && orbit == false) {
            const auto point = PlacementPoint3d(scene, mapping, pixel);
            if (point.has_value() == false) {
                throw std::invalid_argument("choose a point on the Y=2 placement plane inside the movement bounds; "
                                            "orbit the view if the ray is parallel");
            }
            const auto kind = std::exchange(placement_kind_, {});
            ApplyViewportEdit(
                "scene.create_object",
                Encode(SceneCreate{viewport_document_, viewport_revision_, kind,
                                   Value::Object{{"position", Value::Array{(*point)[0], (*point)[1], (*point)[2]}}}}));
            SyncViewport();
            UpdatePreview();
            return;
        }
        if (pan || orbit) {
            viewport_gesture_3d_ = ViewportGesture3d{
                mapping, pixel, {}, {}, pan ? Gesture3dKind::kPan : Gesture3dKind::kOrbit, 0, {}, viewport_revision_};
            return;
        }
        // 選択済みの軸を先に判定し、本体のクリックは選択だけにする。
        for (const auto& object : scene.objects) {
            if (object.id != viewport_selected_ || object.kind == "game_camera") {
                continue;
            }
            const auto position = Point3d{object.position[0], object.position[1], object.position[2]};
            if (const auto axis = HitAxis3d(mapping, position, pixel)) {
                viewport_gesture_3d_ = ViewportGesture3d{
                    mapping, pixel, position, position, Gesture3dKind::kAxis, *axis, object.id, viewport_revision_};
                return;
            }
        }
        viewport_selected_ = PickScene3d(scene, mapping, pixel);
        DrawViewport();
    }

    void EditorCore::PointerMove3d(Point2d pixel)
    {
        auto& drag = *viewport_gesture_3d_;
        const auto dx = pixel[0] - drag.start[0];
        const auto dy = pixel[1] - drag.start[1];
        if (drag.kind == Gesture3dKind::kAxis) {
            // 一単位の軸が画面で何pxになるかを使い、斜めの軸にも同じ操作を適用する。
            const auto axis = Axes3d(drag.mapping, drag.original)[drag.axis].projected_unit;
            const auto delta = (dx * axis[0] + dy * axis[1]) / (axis[0] * axis[0] + axis[1] * axis[1]);
            const auto& settings = scenes_.document()->settings;
            drag.candidate[drag.axis] =
                std::clamp(drag.original[drag.axis] + delta, double(settings.bounds_min[drag.axis]),
                           double(settings.bounds_max[drag.axis]));
        }
        else if (drag.kind == Gesture3dKind::kPan) {
            const auto start = drag.mapping.OnViewPlane(drag.start);
            const auto current = drag.mapping.OnViewPlane(pixel);
            auto view = drag.mapping.view;
            view.center = Add3d(view.center, Subtract3d(start, current));
            ValidateView3d(view);
            viewport_view_3d_ = view;
        }
        else {
            // 極を越えない範囲にして上下の反転を防ぎ、水平角は一周へ正規化する。
            viewport_view_3d_.yaw_degrees = std::remainder(drag.mapping.view.yaw_degrees - dx * .4, 360.0);
            viewport_view_3d_.pitch_degrees = std::clamp(drag.mapping.view.pitch_degrees + dy * .4, -85.0, 85.0);
        }
        DrawViewport();
    }

    void EditorCore::PointerUp3d()
    {
        const auto drag = *viewport_gesture_3d_;
        const auto view = viewport_view_3d_;
        viewport_gesture_3d_.reset();
        // 共通アクションが失敗した場合も、仮表示を確定状態として残さない。
        if (drag.kind != Gesture3dKind::kAxis) {
            viewport_view_3d_ = drag.mapping.view;
        }
        try {
            if (drag.kind == Gesture3dKind::kAxis) {
                ApplyViewportEdit("scene.set_properties",
                                  Encode(SceneSet{viewport_document_,
                                                  drag.revision,
                                                  drag.object_id,
                                                  {{"position", Value::Array{drag.candidate[0], drag.candidate[1],
                                                                             drag.candidate[2]}}}}));
            }
            else {
                ApplyViewportEdit("viewport.set_view", Encode(ViewportSet{viewport_document_,
                                                                          drag.revision,
                                                                          {view.center.begin(), view.center.end()},
                                                                          view.height,
                                                                          view.yaw_degrees,
                                                                          view.pitch_degrees}));
            }
        } catch (...) {
            DrawViewport();
            throw;
        }
        UpdatePreview();
        DrawViewport();
    }

    void EditorCore::ZoomViewport3d(Point2d pixel, double width, double height, double factor)
    {
        const auto mapping = MakeMapping3d(viewport_view_3d_, width, height);
        if (mapping.area.Contains(pixel) == false) {
            return;
        }
        if (std::isfinite(factor) == false || factor <= 0) {
            throw std::invalid_argument("zoom factor must be positive and finite");
        }
        const auto base = FitScene3d(*scenes_.document()).height;
        const auto height_limit =
            std::clamp(mapping.view.height * factor, std::max(1e-6, base / 100), std::min(1e30, base * 100));
        const auto view = ZoomView3d(mapping, pixel, height_limit / mapping.view.height);
        ApplyViewportEdit("viewport.set_view", Encode(ViewportSet{viewport_document_,
                                                                  viewport_revision_,
                                                                  {view.center.begin(), view.center.end()},
                                                                  view.height,
                                                                  view.yaw_degrees,
                                                                  view.pitch_degrees}));
    }
} // namespace gengine::editor
