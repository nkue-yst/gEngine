#include "editor_core.hpp"
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
        const auto state = core.scenes().State();
        return {state.document_id, state.revision};
    }
    Point2d Position(EditorCore& core)
    {
        const auto& p = core.scenes().document()->objects.front().position;
        return {p[0], p[1]};
    }
    Mapping2d Mapping(EditorCore& core)
    {
        const auto state = core.ViewportStatus();
        return MakeMapping2d({{state.center[0], state.center[1]}, state.height}, 640, 360);
    }
    void StartDrag(EditorCore& core, Point2d offset)
    {
        core.SelectViewportObject("player");
        const auto p = Mapping(core).ToPixel(Position(core));
        core.PointerDown({p[0] + offset[0], p[1] + offset[1]}, 640, 360, false);
        Require(core.ViewportStatus().transient, "handle did not start drag");
    }
} // namespace
int wmain(int argc, wchar_t** argv)
{
    try {
        Require(argc == 3, "workspace/compiler required");
        std::filesystem::create_directories(argv[1]);
        auto core = EditorCore(argv[1], argv[2]);
        Call<EditorState>(core, "project.create", ProjectCreateRequest{NewId(), "2d"});
        Call<DocumentInfo>(core, "document.open", DocumentOpen{"scene.json", {}, {}});
        const auto initial = core.scenes().State();
        const auto start = Position(core);
        auto mapping = Mapping(core);
        // 仮移動は文書・履歴へ触れず、解放時だけ一回確定する。
        StartDrag(core, {44, 0});
        auto p = mapping.ToPixel(start);
        core.PointerMove({p[0] + 84, p[1] - 50});
        Require(Position(core) == start && core.scenes().State().revision == initial.revision, "drag mutated document");
        Require(core.ViewportCandidate()->at(1) == start[1], "X drag changed Y");
        const auto capture_error = core.dispatcher().Availability(
            "viewport.capture", 1, Encode(CaptureRequest{{}, initial.document_id, initial.revision}));
        Require(capture_error && capture_error->code == "Busy", "transient capture accepted");
        core.PointerUp();
        auto state = core.scenes().State();
        Require(state.revision == initial.revision + 1 && Position(core)[0] > start[0], "drag not one edit");
        Call<DocumentInfo>(core, "history.undo", HistoryRequest{state.document_id, state.revision, *state.undo_id});
        Require(Position(core) == start, "undo did not restore position");
        state = core.scenes().State();
        Call<DocumentInfo>(core, "history.redo", HistoryRequest{state.document_id, state.revision, *state.redo_id});
        const auto moved = Position(core);
        // 軸の境界、取消し、変更なしを区別する。
        StartDrag(core, {0, -44});
        core.PointerMove({10000, -10000});
        Require(core.ViewportCandidate()->at(0) == moved[0] && core.ViewportCandidate()->at(1) == 4.5, "Y clamp");
        core.CancelViewportGesture();
        Require(Position(core) == moved && core.ViewportStatus().transient == false, "cancel committed");
        const auto before_noop = core.scenes().State().revision;
        StartDrag(core, {16, -16});
        core.PointerUp();
        Require(core.scenes().State().revision == before_noop, "no-op history");
        StartDrag(core, {16, -16});
        core.PointerMove({-10000, 10000});
        core.PointerUp();
        Require(Position(core) == Point2d{-8, -4.5}, "plane clamp");
        // 文書が変わった後に解放しても、古い仮位置を上書きしない。
        Call<DocumentInfo>(core, "scene.set_properties",
                           SceneSet{Target(core).document_id,
                                    Target(core).expected_revision,
                                    "player",
                                    {{"position", Value::Array{0.0, -3.0}}}});
        StartDrag(core, {44, 0});
        core.PointerMove({500, 200});
        Call<DocumentInfo>(core, "scene.set_properties",
                           SceneSet{Target(core).document_id,
                                    Target(core).expected_revision,
                                    "player",
                                    {{"position", Value::Array{2.0, -2.0}}}});
        core.PointerUp();
        Require(Position(core) == Point2d{2, -2} && core.ViewportStatus().transient == false,
                "conflicting drag overwrote edit");
        const auto before_view = core.scenes().State();
        auto view = Call<ViewportState>(core, "viewport.set_view",
                                        ViewportSet{before_view.document_id, before_view.revision, {3, 4}, 18});
        Require(view.center == std::vector<double>{3, 4} && core.scenes().State().revision == before_view.revision &&
                    core.scenes().State().undo_id == before_view.undo_id,
                "view changed history");
        core.ZoomViewport({320, 180}, 640, 360, .5);
        Require(core.ViewportStatus().height == 9, "zoom");
        core.PointerDown({320, 180}, 640, 360, true);
        core.PointerMove({360, 180});
        core.CancelViewportGesture();
        Require(core.ViewportStatus().center == view.center, "cancel pan");
        Call<ViewportState>(core, "viewport.reset_view", Target(core));
        core.SelectViewportObject("enemy");
        core.DeleteViewportObject();
        core.BeginPlacement("enemy_spawn");
        core.CancelViewportGesture();
        Require(core.scenes().document()->objects.size() == 2, "cancel create");
        core.BeginPlacement("enemy_spawn");
        const auto point = Mapping(core).ToPixel({1, 2});
        core.PointerDown(point, 640, 360, false);
        Require(core.scenes().document()->objects.size() == 3 && core.ViewportStatus().selected_id.empty() == false,
                "click create");
        const auto saved = Call<DocumentInfo>(core, "document.save", Target(core));
        const auto reloaded =
            Call<DocumentInfo>(core, "document.reload", DocumentReload{saved.document_id, saved.revision, {}});
        Require(saved.scene == reloaded.scene, "placement save roundtrip");
        std::cout << "viewport selection, axes, clamp, history, cancel, conflict, view and save passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
