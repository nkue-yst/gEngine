#include "editor_core.hpp"
#include "json.hpp"
#include <aclapi.h>
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
    template <class T> Operation Call(EditorCore& editor, std::string id, const T& request, std::string operation = {})
    {
        auto submitted = editor.dispatcher().SubmitTyped(id, request, operation.empty() ? NewId() : operation);
        if (submitted.has_value() == false) {
            throw std::runtime_error(submitted.error().reason);
        }
        editor.dispatcher().Pump();
        return *editor.dispatcher().Lookup(submitted->operation_id).operation;
    }
    template <class Output, class Input> Output Good(EditorCore& editor, std::string id, const Input& request)
    {
        const auto result = Call(editor, id, request);
        if (result.error) {
            throw std::runtime_error(id + ": " + result.error->reason);
        }
        return Decode<Output>(result.result).value();
    }
    void Run(const std::filesystem::path& root, const std::filesystem::path& compiler, const std::string& game)
    {
        auto editor = EditorCore(root, compiler);
        const auto project = *Good<EditorState>(editor, "project.create", ProjectCreateRequest{game, game}).project;
        auto state = Good<DocumentInfo>(editor, "document.open", DocumentOpen{"scene.json", {}, {}});
        const auto doc = state.document_id;
        const auto path = root / Wide(game) / L"scene.json";
        const auto original = ReadFile(path, 1024 * 1024);
        const auto initial = state;
        auto set = SceneSettingsRequest{doc, state.revision, {{"move_speed", 7.5}}};
        const auto first = Call(editor, "scene.set_settings", set, "same-edit");
        Require(first.error.has_value() == false, "edit failed");
        Require(Call(editor, "scene.set_settings", set, "same-edit").result == first.result, "replay differs");
        state = editor.scenes().State();
        Require(state.dirty && state.revision > initial.revision && state.undo_id.has_value(), "edit state");
        const auto conflict = Call(editor, "scene.set_settings", set);
        Require(conflict.error && conflict.error->code == "Conflict" &&
                    conflict.error->current_revision == state.revision,
                "stale edit");
        const auto unchanged = Good<DocumentInfo>(editor, "scene.set_settings",
                                                  SceneSettingsRequest{doc, state.revision, {{"move_speed", 7.5}}});
        Require(unchanged.status == "NoChange" && unchanged.revision == state.revision, "no-change added history");
        const auto replay_conflict = editor.dispatcher().SubmitTyped(
            "scene.set_settings", SceneSettingsRequest{doc, set.expected_revision, {{"move_speed", 9.0}}}, "same-edit");
        Require(replay_conflict.has_value() == false && replay_conflict.error().code == "Conflict",
                "changed replay accepted");
        const auto before_batch = state;
        state = Good<DocumentInfo>(editor, "scene.apply_batch",
                                   SceneBatch{doc,
                                              state.revision,
                                              {{"settings", {}, {}, Value::Object{{"player_hp", 4u}}},
                                               {"settings", {}, {}, Value::Object{{"enemy_hp", 5u}}}}});
        Require(state.revision == before_batch.revision + 1, "batch added multiple revisions");
        state = Good<DocumentInfo>(editor, "history.undo", HistoryRequest{doc, state.revision, *state.undo_id});
        Require(state.scene == before_batch.scene, "batch undo was not atomic");
        auto failure = Call(editor, "scene.apply_batch",
                            SceneBatch{doc,
                                       state.revision,
                                       {{"settings", {}, {}, Value::Object{{"move_speed", 8.0}}},
                                        {"settings", {}, {}, Value::Object{{"enemy_hp", 0u}}}}});
        Require(failure.error.has_value() && editor.scenes().State().scene == state.scene &&
                    editor.scenes().State().revision == state.revision,
                "partial batch commit");
        failure = Call(editor, "history.undo", HistoryRequest{doc, state.revision, "wrong"});
        Require(failure.error && failure.error->code == "Conflict", "wrong history ID");
        state = Good<DocumentInfo>(editor, "history.undo", HistoryRequest{doc, state.revision, *state.undo_id});
        Require(state.dirty == false && state.revision > initial.revision, "undo saved content");
        state = Good<DocumentInfo>(editor, "history.redo", HistoryRequest{doc, state.revision, *state.redo_id});
        const auto close = Call(editor, "project.close", CloseRequest{project.project_id, project.revision});
        Require(close.error && close.error->code == "UnsavedChanges" && close.error->choices.size() == 3,
                "missing close choice");
        Good<EditorState>(editor, "project.close",
                          CloseRequest{project.project_id, project.revision, "cancel", state.revision});
        Require(editor.scenes().State().dirty, "cancel discarded content");
        // 置換できない条件ではディスク内容と編集内容を保持する。
        {
            auto lock =
                Handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
            Require(lock.Valid(), "file lock setup");
            failure = Call(editor, "document.save", DocumentRequest{doc, state.revision});
            Require(failure.error.has_value() && editor.scenes().State().dirty, "locked file save succeeded");
        }
        Require(ReadFile(path, 1024 * 1024) == original, "failed save changed original");
        // 検証用フォルダーだけで一時ファイル作成を拒否し、書込み前の失敗も確認する。
        // 既存ACLは成功・例外のどちらでも復元し、子ファイルへ拒否を継承させない。
        auto directory = Handle(CreateFileW(path.parent_path().c_str(), READ_CONTROL | WRITE_DAC,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        Require(directory.Valid(), "directory ACL handle");
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        PACL old_acl = nullptr;
        Require(GetSecurityInfo(directory.Get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_acl,
                                nullptr, &descriptor) == ERROR_SUCCESS,
                "read directory ACL");
        auto sid = std::array<std::byte, SECURITY_MAX_SID_SIZE>{};
        auto sid_size = DWORD(sid.size());
        Require(CreateWellKnownSid(WinWorldSid, nullptr, sid.data(), &sid_size) != FALSE, "test SID");
        auto deny = EXPLICIT_ACCESSW{};
        deny.grfAccessPermissions = FILE_ADD_FILE;
        deny.grfAccessMode = DENY_ACCESS;
        deny.grfInheritance = NO_INHERITANCE;
        deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        deny.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid.data());
        PACL denied_acl = nullptr;
        Require(SetEntriesInAclW(1, &deny, old_acl, &denied_acl) == ERROR_SUCCESS, "prepare denied write ACL");
        const auto applied_acl = SetSecurityInfo(directory.Get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                                 nullptr, denied_acl, nullptr);
        LocalFree(denied_acl);
        if (applied_acl != ERROR_SUCCESS) {
            LocalFree(descriptor);
            throw std::runtime_error("apply test directory ACL");
        }
        try {
            failure = Call(editor, "document.save", DocumentRequest{doc, state.revision});
        } catch (...) {
            SetSecurityInfo(directory.Get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, old_acl,
                            nullptr);
            LocalFree(descriptor);
            throw;
        }
        const auto restored_acl = SetSecurityInfo(directory.Get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                                  nullptr, old_acl, nullptr);
        LocalFree(descriptor);
        Require(restored_acl == ERROR_SUCCESS, "restore test directory ACL");
        Require(failure.error.has_value() && failure.error->reason.find("temporary") != std::string::npos &&
                    editor.scenes().State().dirty && ReadFile(path, 1024 * 1024) == original,
                "temporary write failure lost data");
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        failure = Call(editor, "document.save", DocumentRequest{doc, state.revision});
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        Require(failure.error.has_value() && ReadFile(path, 1024 * 1024) == original, "readonly save changed original");
        state = Good<DocumentInfo>(editor, "document.save", DocumentRequest{doc, state.revision});
        Require(state.dirty == false, "saved document is dirty");
        const auto saved = ReadFile(path, 1024 * 1024);
        WriteAtomicFile(path, "broken JSON");
        failure = Call(editor, "document.reload", DocumentReload{doc, state.revision, "discard"});
        Require(failure.error.has_value() && editor.scenes().State().scene == state.scene, "bad reload lost document");
        failure = Call(editor, "document.save", DocumentRequest{doc, state.revision});
        Require(failure.error && failure.error->code == "Conflict", "external change overwritten");
        WriteAtomicFile(path, saved);
        state = Good<DocumentInfo>(editor, "document.reload", DocumentReload{doc, state.revision, {}});
        Require(state.undo_id.has_value() == false && state.revision > initial.revision, "reload retained history");
        state = Good<DocumentInfo>(editor, "scene.delete_objects", SceneDelete{doc, state.revision, {"enemy"}});
        state = Good<DocumentInfo>(editor, "scene.create_object", SceneCreate{doc, state.revision, "enemy_spawn", {}});
        auto page = Good<DocumentInfo>(editor, "scene.find_objects", SceneQuery{doc, {}, {}, 1, {}});
        Require(page.next_cursor.has_value(), "pagination missing");
        state = Good<DocumentInfo>(editor, "scene.set_settings",
                                   SceneSettingsRequest{doc, state.revision, {{"player_hp", 4u}}});
        failure = Call(editor, "scene.find_objects", SceneQuery{doc, {}, {}, 1, page.next_cursor});
        Require(failure.error && failure.error->code == "Conflict", "stale cursor");
        // 履歴上限を超えた後も現在値と次のUndoを維持する。
        for (auto i = 0; i < 260; ++i) {
            state =
                Good<DocumentInfo>(editor, "scene.set_settings",
                                   SceneSettingsRequest{doc, state.revision, {{"player_hp", std::uint32_t(i + 5)}}});
        }
        const auto latest = state.scene;
        for (auto i = 0; i < 256; ++i) {
            Require(state.undo_id.has_value(), "history prematurely trimmed");
            state = Good<DocumentInfo>(editor, "history.undo", HistoryRequest{doc, state.revision, *state.undo_id});
        }
        Require(state.undo_id.has_value() == false, "history exceeded 256 entries");
        for (auto i = 0; i < 256; ++i) {
            state = Good<DocumentInfo>(editor, "history.redo", HistoryRequest{doc, state.revision, *state.redo_id});
        }
        Require(state.scene == latest, "bounded history changed values");
        state = Good<DocumentInfo>(editor, "history.undo", HistoryRequest{doc, state.revision, *state.undo_id});
        state = Good<DocumentInfo>(editor, "scene.set_settings",
                                   SceneSettingsRequest{doc, state.revision, {{"player_hp", 300u}}});
        Require(state.redo_id.has_value() == false, "new edit retained redo branch");
        const auto recovery_scene = state.scene;
        editor.Shutdown();
        auto restored = EditorCore(root, compiler);
        Good<EditorState>(restored, "project.open", ProjectPathRequest{game + "/project.gengine.json"});
        state = Good<DocumentInfo>(restored, "document.open", DocumentOpen{"scene.json", {}, {}});
        Require(state.recovery_available, "disconnect lost recovery");
        const auto inspected =
            Good<DocumentInfo>(restored, "document.recover", RecoveryRequest{doc, state.revision, "inspect"});
        Require(inspected.recovery.has_value(), "recovery contents unavailable");
        state = Good<DocumentInfo>(restored, "document.recover", RecoveryRequest{doc, state.revision, "restore"});
        Require(state.scene == recovery_scene && state.dirty, "recovery changed content");
        Require(ReadFile(path, 1024 * 1024) == saved, "recovery overwrote disk scene");
        state = Good<DocumentInfo>(restored, "document.save", DocumentRequest{doc, state.revision});
        Require(state.recovery_available == false && state.dirty == false, "save retained recovery");
    }
} // namespace
int wmain(int argc, wchar_t** argv)
{
    try {
        Require(argc == 3, "workspace and compiler required");
        const auto root = std::filesystem::path(argv[1]) / Wide(NewId());
        std::filesystem::create_directories(root);
        Run(root, argv[2], "2d");
        Run(root, argv[2], "3d");
        std::cout << "scene editing, history, save and recovery passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
