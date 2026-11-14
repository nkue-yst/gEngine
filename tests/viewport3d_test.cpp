#include "editor_core.hpp"
#include "picking3d.hpp"
#include "preview3d.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace gengine;
using namespace gengine::editor;
namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition == false) {
            throw std::runtime_error(message);
        }
    }
    bool Close(double a, double b)
    {
        return std::abs(a - b) < 1e-6;
    }
    template <class Output, class Input> Output Call(EditorCore& core, std::string action, Input input)
    {
        const auto result = core.dispatcher().SubmitTyped(action, input, NewId());
        Require(result.has_value(), "submit failed");
        core.dispatcher().Pump();
        const auto operation = core.dispatcher().Lookup(result->operation_id).operation;
        if (operation->error) {
            throw std::runtime_error(action + ": " + operation->error->reason);
        }
        return Decode<Output>(operation->result).value();
    }
    DocumentRequest Target(EditorCore& core)
    {
        return {core.scenes().State().document_id, core.scenes().Revision()};
    }
    Point3d Position(EditorCore& core)
    {
        const auto& p = core.scenes().document()->objects.front().position;
        return {p[0], p[1], p[2]};
    }
    Mapping3d Mapping(EditorCore& core)
    {
        const auto s = core.ViewportStatus();
        return MakeMapping3d({{s.center[0], s.center[1], s.center[2]}, s.height, *s.yaw_degrees, *s.pitch_degrees}, 640,
                             360);
    }
    Point2d StartDrag(EditorCore& core, unsigned axis)
    {
        core.SelectViewportObject("player");
        const auto m = Mapping(core);
        const auto center = m.ToPixel(Position(core));
        const auto d = Axes3d(m, Position(core))[axis].direction;
        const auto pixel = Point2d{center[0] + d[0] * 44, center[1] + d[1] * 44};
        core.PointerDown(pixel, 640, 360, false);
        Require(core.ViewportCandidate().has_value(), "axis drag not started");
        return pixel;
    }
    void Undo(EditorCore& core)
    {
        const auto s = core.scenes().State();
        Call<DocumentInfo>(core, "history.undo", HistoryRequest{s.document_id, s.revision, *s.undo_id});
    }
    void CheckGeometry()
    {
        auto scene = DefaultScene(true);
        const auto fitted = FitScene3d(scene);
        for (const auto size : {Point2d{640, 360}, Point2d{300, 800}, Point2d{1200, 300}}) {
            const auto m = MakeMapping3d(fitted, size[0], size[1]);
            const auto pixel = Point2d{size[0] * .4, size[1] * .5};
            const auto again = m.ToPixel(m.OnViewPlane(pixel));
            Require(Close(again[0], pixel[0]) && Close(again[1], pixel[1]), "projection roundtrip");
            const auto ray = m.Ray(pixel);
            const auto on_plane = Add3d(ray.origin, Multiply3d(ray.direction, m.Distance()));
            Require(Close(Dot3d(Subtract3d(on_plane, m.OnViewPlane(pixel)), m.up), 0),
                    "ray does not pass screen point");
            Require(Close(Dot3d(m.right, m.up), 0) && Close(Dot3d(m.back, m.up), 0), "camera basis not orthogonal");
            const auto zoom = MakeMapping3d(ZoomView3d(m, pixel, .5), size[0], size[1]);
            const auto anchored = zoom.ToPixel(m.OnViewPlane(pixel));
            Require(Close(anchored[0], pixel[0]) && Close(anchored[1], pixel[1]), "zoom moved anchor");
            for (auto i = 0u; i < 8; ++i) {
                auto corner = Point3d{};
                for (auto axis = 0u; axis < 3; ++axis) {
                    corner[axis] =
                        (i & (1u << axis)) ? scene.settings.bounds_min[axis] : scene.settings.bounds_max[axis];
                }
                Require(m.area.Contains(m.ToPixel(corner)), "fit clipped movement bounds");
            }
        }
        const auto m = MakeMapping3d({{0, 2, 0}, 18, 0, 0}, 640, 360);
        Require(m.ToPixel({1, 3, 0}) == Point2d{340, 160}, "X right / Y up / scale");
        Require(Axes3d(m, {0, 2, 0})[2].visible == false, "view-aligned axis not hidden");
        Require(Axes3d(m, {0, 2, 1000})[0].visible == false, "clipped object retained handles");
        auto distant_camera = DefaultScene(true);
        distant_camera.objects.erase(distant_camera.objects.begin(), distant_camera.objects.begin() + 2);
        distant_camera.objects[0].position = {0, 2, 1000};
        distant_camera.objects[0].target = {0, 2, 1001};
        Require(PickScene3d(distant_camera, m, {320, 180}).empty(), "clipped camera remained selectable");
        auto matrix = std::array<float, 16>{};
        EditorCamera3d(matrix.data(), m);
        Require(Close(matrix[0] * 1 + matrix[12], 1.0 / 16) && Close(matrix[5] * 3 + matrix[13], 1.0 / 9),
                "GPU projection differs from picking");
        scene.objects[0].position = {0, 2, 3};
        scene.objects[1].position = {0, 2, -3};
        Require(PickScene3d(scene, m, {320, 180}) == "player", "nearest sphere not selected");
        std::swap(scene.objects[0].position, scene.objects[1].position);
        Require(PickScene3d(scene, m, {320, 180}) == "enemy", "depth ignored");
        Require(PickScene3d(scene, MakeMapping3d(fitted, 300, 800), {10, 10}).empty(), "letterbox selected");
        Require(PlacementPoint3d(scene, m, {320, 180}).has_value() == false, "parallel placement accepted");
        const auto angled = MakeMapping3d({{0, 2, 0}, 24, 45, 30}, 640, 360);
        const auto placed = PlacementPoint3d(scene, angled, angled.ToPixel({1, 2, -1}));
        Require(placed && Close((*placed)[0], 1) && Close((*placed)[2], -1), "plane intersection");
        Require(PlacementPoint3d(scene, angled, {0, 0}).has_value() == false, "outside placement accepted");
        const auto pos = Point3d{0, 2, 0};
        const auto center = angled.ToPixel(pos);
        for (auto axis = 0u; axis < 3; ++axis) {
            const auto d = Axes3d(angled, pos)[axis].direction;
            Require(HitAxis3d(angled, pos, {center[0] + 44 * d[0], center[1] + 44 * d[1]}) == axis,
                    "axis hit mismatch");
        }
        Require(HitAxis3d(angled, pos, center).has_value() == false, "body treated as handle");
        const auto camera_pixel = angled.ToPixel({12, 10, 14});
        Require(PickScene3d(scene, angled, camera_pixel) == "camera", "camera outline not selected");
        const auto geometry = BuildPreview3d(scene, angled, "player");
        Require(geometry.line_count == 42 && geometry.overlay_count == 10, "missing bounds / camera / handles");
    }
    void CheckActions(const std::filesystem::path& root, const std::filesystem::path& compiler)
    {
        auto core = EditorCore(root, compiler);
        Call<EditorState>(core, "project.create", ProjectCreateRequest{NewId(), "3d"});
        Call<DocumentInfo>(core, "document.open", DocumentOpen{"scene.json", {}, {}});
        const auto original = Position(core);
        for (auto axis = 0u; axis < 3; ++axis) {
            const auto before = core.scenes().State();
            const auto start = StartDrag(core, axis);
            const auto direction = Axes3d(Mapping(core), original)[axis].projected_unit;
            core.PointerMove({start[0] + direction[0], start[1] + direction[1]});
            Require(Position(core) == original && core.scenes().Revision() == before.revision,
                    "tentative drag changed document");
            const auto candidate = *core.ViewportCandidate();
            for (auto component = 0u; component < 3; ++component) {
                Require(Close(candidate[component], original[component] + (component == axis ? 1 : 0)),
                        "axis changed other component");
            }
            const auto error = core.dispatcher().Availability(
                "viewport.capture", 1, Encode(CaptureRequest{{}, before.document_id, before.revision}));
            Require(error && error->code == "Busy", "tentative capture accepted");
            core.PointerUp();
            Require(core.scenes().Revision() == before.revision + 1, "drag not one revision");
            Undo(core);
            Require(Position(core) == original, "one undo did not restore drag");
            const auto undone = core.scenes().State();
            Call<DocumentInfo>(core, "history.redo",
                               HistoryRequest{undone.document_id, undone.revision, *undone.redo_id});
            Require(Close(Position(core)[axis], original[axis] + 1), "redo lost drag");
            Undo(core);
            const auto idle_revision = core.scenes().Revision();
            StartDrag(core, axis);
            core.PointerUp();
            Require(core.scenes().Revision() == idle_revision, "unchanged drag added history");
            StartDrag(core, axis);
            core.PointerMove({start[0] + direction[0] * 1000, start[1] + direction[1] * 1000});
            Require(core.ViewportCandidate()->at(axis) == core.scenes().document()->settings.bounds_max[axis],
                    "axis did not stop at bound");
            core.CancelViewportGesture();
            Require(Position(core) == original && core.scenes().Revision() == idle_revision, "cancel changed document");
        }
        StartDrag(core, 2);
        core.PointerMove({100, 100});
        auto target = Target(core);
        Call<DocumentInfo>(
            core, "scene.set_properties",
            SceneSet{
                target.document_id, target.expected_revision, "player", {{"position", Value::Array{1.0, 3.0, 2.0}}}});
        core.PointerUp();
        Require(Position(core) == Point3d{1, 3, 2} && core.ViewportStatus().transient == false,
                "external edit overwritten");
        StartDrag(core, 0);
        target = Target(core);
        Call<DocumentInfo>(core, "scene.delete_objects",
                           SceneDelete{target.document_id, target.expected_revision, {"player"}});
        core.PointerUp();
        Require(core.ViewportStatus().selected_id.empty(), "deleted selection retained");
        Undo(core);
        const auto before_view = core.scenes().State();
        const auto old_view = core.ViewportStatus();
        core.PointerDown({320, 180}, 640, 360, true);
        core.PointerMove({350, 200});
        core.CancelViewportGesture();
        Require(core.ViewportStatus().center == old_view.center, "cancelled pan persisted");
        core.PointerDown({320, 180}, 640, 360, false, true);
        core.PointerMove({350, 200});
        core.PointerUp();
        auto view = core.ViewportStatus();
        Require(Close(*view.yaw_degrees, *old_view.yaw_degrees - 12) &&
                    Close(*view.pitch_degrees, *old_view.pitch_degrees + 8),
                "orbit not applied");
        core.ZoomViewport({320, 180}, 640, 360, .5);
        Require(Close(core.ViewportStatus().height, old_view.height * .5), "3D zoom");
        Require(core.scenes().Revision() == before_view.revision &&
                    core.scenes().State().undo_id == before_view.undo_id &&
                    core.scenes().State().dirty == before_view.dirty,
                "view mutated document");
        Call<ViewportState>(core, "viewport.reset_view", Target(core));
        core.SelectViewportObject("enemy");
        core.DeleteViewportObject();
        core.BeginPlacement("enemy_spawn");
        core.CancelViewportGesture();
        Require(core.scenes().document()->objects.size() == 2, "cancel created object");
        core.BeginPlacement("enemy_spawn");
        core.PointerDown(Mapping(core).ToPixel({1, 2, -2}), 640, 360, false);
        Require(core.scenes().document()->objects.size() == 3, "placement missing");
        auto duplicate_rejected = false;
        try {
            core.BeginPlacement("enemy_spawn");
        } catch (const std::invalid_argument&) {
            duplicate_rejected = true;
        }
        Require(duplicate_rejected, "duplicate kind accepted");
        const auto saved = Call<DocumentInfo>(core, "document.save", Target(core));
        view = core.ViewportStatus();
        const auto reload =
            Call<DocumentInfo>(core, "document.reload", DocumentReload{saved.document_id, saved.revision, {}});
        Require(saved.scene == reload.scene && core.ViewportStatus().center == view.center,
                "saved placement/view changed on reload");
        // 同じ要求IDの再送は履歴を二重に増やさない。
        target = Target(core);
        const auto request = Request{
            "scene.set_properties", 1, NewId(),
            Encode(SceneSet{
                target.document_id, target.expected_revision, "player", {{"position", Value::Array{0.0, 2.0, 4.0}}}})};
        const auto first = core.dispatcher().Submit(request);
        core.dispatcher().Pump();
        const auto revision = core.scenes().Revision();
        const auto repeated = core.dispatcher().Submit(request);
        core.dispatcher().Pump();
        Require(first && repeated && first->operation_id == repeated->operation_id &&
                    core.scenes().Revision() == revision,
                "replay applied twice");
        const auto conflict = core.dispatcher().Availability("scene.set_properties", 1, request.arguments);
        Require(conflict && conflict->code == "Conflict", "stale revision accepted");
        // 解放直前に届いてまだ実行されていない要求とも、Dispatcherで競合を検出する。
        StartDrag(core, 0);
        core.PointerMove({400, 180});
        target = Target(core);
        const auto queued = core.dispatcher().SubmitTyped(
            "scene.set_properties",
            SceneSet{
                target.document_id, target.expected_revision, "player", {{"position", Value::Array{2.0, 2.0, 3.0}}}},
            NewId());
        Require(queued.has_value(), "concurrent edit not queued");
        auto rejected = false;
        try {
            core.PointerUp();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        Require(rejected && Position(core) == Point3d{2, 2, 3} &&
                    core.scenes().Revision() == target.expected_revision + 1,
                "release overwrote queued external edit");
        // 文書切替えでは仮位置と編集カメラを引き継がない。
        const auto project = *Call<EditorState>(core, "editor.get_state", EmptyRequest{}).project;
        StartDrag(core, 2);
        core.PointerMove({350, 160});
        Call<EditorState>(core, "project.close",
                          CloseRequest{project.project_id, project.revision, "save", core.scenes().Revision()});
        Call<EditorState>(core, "project.create", ProjectCreateRequest{NewId(), "2d"});
        Call<DocumentInfo>(core, "document.open", DocumentOpen{"scene.json", {}, {}});
        core.PointerUp();
        const auto switched = core.ViewportStatus();
        Require(switched.center.size() == 2 && switched.yaw_degrees.has_value() == false && switched.transient == false,
                "document switch retained 3D gesture/view");
    }
} // namespace
int wmain(int argc, wchar_t** argv)
{
    try {
        Require(argc == 3, "workspace/compiler required");
        std::filesystem::create_directories(argv[1]);
        CheckGeometry();
        CheckActions(argv[1], argv[2]);
        std::cout
            << "3D projection, ray depth, handles, placement plane, transient/history/conflict/view/save passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
