#include "editor_core.hpp"
#include "json.hpp"
#include <iostream>

using namespace gengine::editor;
namespace
{
    void Require(bool value, const char* reason)
    {
        if (value == false) {
            throw std::runtime_error(reason);
        }
    }
    template <class T>
    Operation Call(EditorCore& editor, const std::string& id, const T& request, std::string operation = {})
    {
        const auto submitted = editor.dispatcher().SubmitTyped(id, request, operation.empty() ? NewId() : operation);
        Require(submitted.has_value(), "submission failed");
        editor.dispatcher().Pump();
        return *editor.dispatcher().Lookup(submitted->operation_id).operation;
    }
    template <class T, class R> T Good(EditorCore& editor, const std::string& id, const R& request)
    {
        const auto result = Call(editor, id, request);
        if (result.error) {
            throw std::runtime_error(result.error->reason);
        }
        return Decode<T>(result.result).value();
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    try {
        Require(argc == 4, "output, compiler and content required");
        const auto root = std::filesystem::path(argv[1]) / Wide(NewId());
        std::filesystem::create_directories(root);
        auto editor = EditorCore(root, argv[2]);
        auto state = Good<EditorState>(editor, "project.create", ProjectCreateRequest{"project", "2d"});
        const auto project_path = root / "project/project.gengine.json";
        const auto content = std::filesystem::path(argv[3]);
        const auto original_project = ReadFile(project_path, 64 * 1024);
        auto request = AssetRegister{state.project->project_id, state.project->revision, "missing.png"};
        Require(Call(editor, "asset.register", request).error.has_value(), "missing asset accepted");
        Require(ReadFile(project_path, 64 * 1024) == original_project, "failure changed project");

        // 同じ原本を変更せず登録し、形式・再送・古いrevision・通常再読込みを照合する。
        for (const auto* name : {"sprite.png", "sphere.gmesh", "shot.wav"}) {
            const auto original = ReadFile(content / "assets" / name, 16 * 1024 * 1024);
            WriteNewFile(root / "project" / name, original);
            request = {state.project->project_id, state.project->revision, name};
            const auto operation_id = NewId();
            const auto result = Call(editor, "asset.register", request, operation_id);
            Require(result.error.has_value() == false, "valid asset rejected");
            Require(Call(editor, "asset.register", request, operation_id).result == result.result,
                    "replay changed result");
            state = Decode<EditorState>(result.result).value();
            Require(ReadFile(root / "project" / name, 16 * 1024 * 1024) == original, "original changed");
            Require(Call(editor, "asset.register", request).error->code == "Conflict", "stale revision accepted");
            request.expected_revision = state.project->revision;
            const auto duplicate = Good<EditorState>(editor, "asset.register", request);
            Require(duplicate.project->revision == state.project->revision, "duplicate added revision");
        }
        auto catalog = Good<AssetCatalog>(editor, "asset.list", EmptyRequest{});
        Require(catalog.entries.size() == 3 && catalog.entries[0].kind == "image" &&
                    catalog.entries[1].kind == "mesh" && catalog.entries[2].kind == "audio",
                "catalog types");
        for (const auto& entry : catalog.entries) {
            Require(entry.diagnostic.empty() && entry.bytes > 0 && entry.summary.empty() == false, "catalog metadata");
        }
        for (const auto* path : {"../outside.png", "C:/outside.png", ".gengine/secret.png", "bad.png", "bad.txt"}) {
            if (std::string_view(path).starts_with("bad")) {
                WriteNewFile(root / "project" / path, "not an asset");
            }
            request.path = path;
            Require(Call(editor, "asset.register", request).error.has_value(), "invalid asset accepted");
        }
        // 既存設定の外部変更や置換拒否では、メモリ内の登録一覧を進めない。
        WriteNewFile(root / "project/second.wav", ReadFile(content / "assets/hit.wav", 1024 * 1024));
        request.path = "second.wav";
        const auto saved = ReadFile(project_path, 64 * 1024);
        WriteAtomicFile(project_path, saved + " ");
        Require(Call(editor, "asset.register", request).error->code == "Conflict", "external edit overwritten");
        WriteAtomicFile(project_path, saved);
        {
            auto lock = Handle(
                CreateFileW(project_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
            Require(lock.Valid() && Call(editor, "asset.register", request).error.has_value(), "locked save succeeded");
        }
        Require(ReadFile(project_path, 64 * 1024) == saved, "failed registration changed disk");
        WriteAtomicFile(root / "project/sprite.png", "broken");
        catalog = Good<AssetCatalog>(editor, "asset.list", EmptyRequest{});
        Require(catalog.entries[0].diagnostic.empty() == false && catalog.entries[1].diagnostic.empty(),
                "diagnostics lost");
        Good<EditorState>(editor, "project.close", CloseRequest{state.project->project_id, state.project->revision});
        state = Good<EditorState>(editor, "project.open", ProjectPathRequest{"project/project.gengine.json"});
        Require(state.project->assets.size() == 3, "registration not persisted");
        Require(Good<AssetCatalog>(editor, "asset.list", EmptyRequest{}).entries[0].diagnostic.empty() == false,
                "broken registered asset blocked project or lost reason");
        WriteAtomicFile(root / "project/sprite.png", ReadFile(content / "assets/sprite.png", 1024 * 1024));
        auto doc = Good<DocumentInfo>(editor, "document.open", DocumentOpen{"scene.json", {}, {}});
        const auto change = [&](Value::Object properties) {
            return Call(editor, "scene.set_properties",
                        SceneSet{doc.document_id, doc.revision, "player", std::move(properties)});
        };
        Require(change({{"visual", "sphere.gmesh"}}).error.has_value(), "2D mesh accepted");
        Require(change({{"audio", "sprite.png"}}).error.has_value(), "PNG accepted as audio");
        auto changed = change({{"visual", "sprite.png"}, {"audio", "shot.wav"}});
        Require(changed.error.has_value() == false, "valid references rejected");
        doc = Decode<DocumentInfo>(changed.result).value();
        const auto assigned = doc.scene;
        doc = Good<DocumentInfo>(editor, "history.undo", HistoryRequest{doc.document_id, doc.revision, *doc.undo_id});
        Require(editor.scenes().document()->objects[0].visual.has_value() == false, "assignment undo failed");
        doc = Good<DocumentInfo>(editor, "history.redo", HistoryRequest{doc.document_id, doc.revision, *doc.redo_id});
        Require(doc.scene == assigned, "assignment redo failed");
        doc = Good<DocumentInfo>(editor, "document.save", DocumentRequest{doc.document_id, doc.revision});
        Require(gengine::LoadProjectScene(project_path, "scene.json").objects[0].visual == "sprite.png",
                "saved reference lost");

        // 原本が壊れても文書を開き、他の値の編集・保存・解除・履歴による復元を続けられる。
        WriteAtomicFile(root / "project/sprite.png", "broken");
        doc = Good<DocumentInfo>(editor, "document.reload", DocumentReload{doc.document_id, doc.revision, {}});
        catalog = Good<AssetCatalog>(editor, "asset.list", EmptyRequest{});
        Require(catalog.references.size() == 2 && catalog.references[0].object_id == "player" &&
                    catalog.references[0].slot == "visual" && catalog.references[0].diagnostic.empty() == false,
                "reference diagnostic missing target");
        doc = Good<DocumentInfo>(editor, "scene.set_settings",
                                 SceneSettingsRequest{doc.document_id, doc.revision, {{"move_speed", 6.0}}});
        doc = Good<DocumentInfo>(editor, "document.save", DocumentRequest{doc.document_id, doc.revision});
        const auto before_bad = doc;
        changed = change({{"visual", "missing.png"}, {"audio", Value{}}});
        Require(changed.error.has_value() && editor.scenes().State().scene == before_bad.scene &&
                    editor.scenes().Revision() == before_bad.revision,
                "invalid reference partially applied");
        doc = Decode<DocumentInfo>(change({{"visual", Value{}}}).result).value();
        Require(editor.scenes().document()->objects[0].visual.has_value() == false, "clear failed");
        doc = Good<DocumentInfo>(editor, "history.undo", HistoryRequest{doc.document_id, doc.revision, *doc.undo_id});
        Require(editor.scenes().document()->objects[0].visual == "sprite.png",
                "undo could not restore broken reference");
        WriteAtomicFile(root / "project/sprite.png", ReadFile(content / "assets/sprite.png", 1024 * 1024));
        Require(Good<AssetCatalog>(editor, "asset.list", EmptyRequest{}).references[0].diagnostic.empty(),
                "repair did not clear diagnostic");
        Good<DocumentInfo>(editor, "document.save", DocumentRequest{doc.document_id, doc.revision});
        std::cout << "asset registration, persistence, validation and conflict passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
