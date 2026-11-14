#include "scene_actions.hpp"
#include "assets.hpp"
#include "json.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

namespace gengine::editor
{
    namespace
    {
        template <class F> Result<DocumentInfo> Guard(F function)
        {
            try {
                return function();
            } catch (const Json::exception& error) {
                return std::unexpected(ActionError{"InvalidArgument", "scene", error.what()});
            } catch (const std::invalid_argument& error) {
                return std::unexpected(ActionError{"InvalidArgument", "scene", error.what()});
            } catch (const std::exception& error) {
                return std::unexpected(ActionError{"IoError", "path", error.what()});
            }
        }
        void Patch(Json& target, const Value::Object& properties)
        {
            for (const auto& [key, value] : properties) {
                if (key == "id" || key == "kind" || target.contains(key) == false) {
                    throw std::invalid_argument("unknown or immutable property: " + key);
                }
                target[key] = AsJson(value);
            }
        }
    } // namespace

    void SceneActions::Attach(std::filesystem::path root, std::vector<std::string> paths, Resolver resolver,
                              std::function<bool()> busy)
    {
        Detach();
        root_ = std::move(root);
        paths_ = std::move(paths);
        resolve_ = std::move(resolver);
        busy_ = std::move(busy);
    }

    void SceneActions::Detach()
    {
        document_.reset();
        path_.clear();
        saved_.clear();
        disk_hash_.clear();
        history_.clear();
        cursor_ = 0;
        paths_.clear();
        resolve_ = {};
        busy_ = {};
        root_.clear();
        // revisionは閉じても戻さず、再読込み前の要求を失効させる。
    }

    DocumentInfo SceneActions::State() const
    {
        auto result = DocumentInfo{};
        if (document_) {
            result.document_id = document_->document_id;
            result.path = path_;
            result.revision = revision_;
            result.before_revision = revision_;
            result.scene = std::get<Value::Object>(FromJson(ParseJson(SerializeScene(*document_))).data);
            result.dirty = SerializeScene(*document_) != saved_;
            if (cursor_ > 0) {
                result.undo_id = history_[cursor_ - 1].id;
            }
            if (cursor_ < history_.size()) {
                result.redo_id = history_[cursor_].id;
            }
            result.recovery_available = std::filesystem::exists(RecoveryPath());
        }
        return result;
    }

    std::optional<ActionError> SceneActions::Check(std::string_view id, std::uint64_t revision, bool writing) const
    {
        if (document_.has_value() == false || id != document_->document_id) {
            return ActionError{"NotFound", "document_id", "document is not open"};
        }
        if (revision != revision_) {
            return ActionError{"Conflict", "expected_revision", "document revision changed", revision_};
        }
        if (writing && busy_ && busy_()) {
            return ActionError{"Busy", "document_id", "stop play before editing the document"};
        }
        return {};
    }

    Result<DocumentInfo> SceneActions::ResolveUnsaved(std::optional<std::string> choice,
                                                      std::optional<std::uint64_t> expected_revision)
    {
        if (document_.has_value() == false) {
            return State();
        }
        if (expected_revision.has_value() && *expected_revision != revision_) {
            return std::unexpected(
                ActionError{"Conflict", "expected_revision", "document revision changed", revision_});
        }
        if (choice && *choice != "save" && *choice != "discard" && *choice != "cancel") {
            return std::unexpected(ActionError{"InvalidArgument", "choice", "choose save, discard or cancel"});
        }
        if (choice == "cancel") {
            auto result = State();
            result.status = "Cancelled";
            return result;
        }
        if (State().dirty == false) {
            return State();
        }
        if (choice.has_value() == false || expected_revision.has_value() == false) {
            return std::unexpected(ActionError{"UnsavedChanges",
                                               "choice",
                                               "unsaved scene requires a choice and revision",
                                               revision_,
                                               {"save", "discard", "cancel"}});
        }
        if (*choice == "save") {
            return Save();
        }
        ClearRecovery();
        return State();
    }

    Result<DocumentInfo> SceneActions::Open(const DocumentOpen& request)
    {
        if (resolve_ == nullptr) {
            return std::unexpected(ActionError{"InvalidState", "path", "open a project first"});
        }
        if (busy_()) {
            return std::unexpected(ActionError{"Busy", "path", "stop play before opening a scene"});
        }
        if (std::find(paths_.begin(), paths_.end(), request.path) == paths_.end()) {
            return std::unexpected(ActionError{"InvalidArgument", "path", "scene must be registered in project"});
        }
        // 切替え先の破損で現在の文書を失わないよう、保存・破棄の前に候補を検証する。
        auto bytes = ReadFile(resolve_(request.path, false), 1024 * 1024);
        auto candidate = ParseScene(bytes);
        // 参照切れも保持し、読込み・履歴・復旧から修正できるようにする。
        auto choice = ResolveUnsaved(request.choice, request.expected_revision);
        if (choice.has_value() == false || choice->status == "Cancelled") {
            return choice;
        }
        // 同じファイルを保存して再読込みした場合は保存後の内容を読む。
        if (request.path == path_ && request.choice == "save") {
            bytes = ReadFile(resolve_(request.path, false), 1024 * 1024);
            candidate = ParseScene(bytes);
        }
        document_ = std::move(candidate);
        path_ = request.path;
        saved_ = SerializeScene(*document_);
        disk_hash_ = Sha256(bytes);
        history_.clear();
        cursor_ = 0;
        ++revision_;
        return State();
    }

    Result<DocumentInfo> SceneActions::Save()
    {
        // 素材の欠落は診断として扱い、修正途中の文書も保存できる。
        const auto path = resolve_(path_, false);
        if (Sha256(ReadFile(path, 1024 * 1024)) != disk_hash_) {
            return std::unexpected(ActionError{"Conflict", "path", "scene file changed outside Editor", revision_});
        }
        const auto bytes = SerializeScene(*document_);
        WriteAtomicFile(path, bytes);
        saved_ = bytes;
        disk_hash_ = Sha256(bytes);
        ClearRecovery();
        return State();
    }

    Result<DocumentInfo> SceneActions::Edit(const std::vector<SceneEdit>& edits)
    {
        if (edits.empty() || edits.size() > 256) {
            return std::unexpected(ActionError{"InvalidArgument", "edits", "batch requires 1..256 edits"});
        }
        const auto before = SerializeScene(*document_);
        auto candidate = ParseJson(before);
        auto changed = std::set<std::string>{};
        // バッチ全体を候補へ適用する。途中では実文書も履歴も更新しない。
        for (const auto& edit : edits) {
            auto& objects = candidate["objects"];
            if (edit.edit == "settings") {
                if (edit.object_id || edit.kind || edit.properties.has_value() == false) {
                    throw std::invalid_argument("settings edit requires only properties");
                }
                Patch(candidate["settings"], *edit.properties);
                continue;
            }
            if (edit.edit == "create") {
                if (edit.object_id || edit.kind.has_value() == false) {
                    throw std::invalid_argument("create requires kind");
                }
                auto base = DefaultScene(document_->game == "3d");
                // 同じバッチで先に変更された範囲を使って追加対象の初期位置を決める。
                const auto& settings = candidate["settings"];
                base.settings.bounds_min = settings.at("bounds_min").get<std::vector<float>>();
                base.settings.bounds_max = settings.at("bounds_max").get<std::vector<float>>();
                base.objects = {DefaultSceneObject(document_->game, *edit.kind, NewId())};
                if (*edit.kind == "player_spawn" || *edit.kind == "enemy_spawn") {
                    const auto size = base.objects[0].position.size();
                    if (base.settings.bounds_min.size() != size || base.settings.bounds_max.size() != size) {
                        throw std::invalid_argument("invalid bounds component count");
                    }
                    for (auto axis = 0u; axis < base.objects[0].position.size(); ++axis) {
                        if (std::isfinite(base.settings.bounds_min[axis]) == false ||
                            std::isfinite(base.settings.bounds_max[axis]) == false ||
                            base.settings.bounds_min[axis] >= base.settings.bounds_max[axis]) {
                            throw std::invalid_argument("invalid bounds range");
                        }
                        base.objects[0].position[axis] =
                            std::clamp(base.objects[0].position[axis], base.settings.bounds_min[axis],
                                       base.settings.bounds_max[axis]);
                    }
                }
                auto object = ParseJson(SerializeScene(base))["objects"][0];
                if (edit.properties) {
                    Patch(object, *edit.properties);
                }
                changed.insert(object["id"].get<std::string>());
                objects.push_back(std::move(object));
                continue;
            }
            if (edit.object_id.has_value() == false || edit.kind) {
                throw std::invalid_argument("edit requires object_id");
            }
            const auto found = std::find_if(objects.begin(), objects.end(), [&](const auto& object) {
                return object["id"] == *edit.object_id;
            });
            if (found == objects.end()) {
                return std::unexpected(ActionError{"NotFound", "object_id", "object does not exist"});
            }
            changed.insert(*edit.object_id);
            if (edit.edit == "delete" && edit.properties.has_value() == false) {
                objects.erase(found);
            }
            else if (edit.edit == "set" && edit.properties) {
                Patch(*found, *edit.properties);
            }
            else {
                throw std::invalid_argument("unknown edit or invalid fields");
            }
        }
        auto parsed = ParseScene(candidate.dump());
        // 新しく指定した参照だけを検証する。既存の欠落は他の編集や参照解除を妨げない。
        auto source = std::optional<AssetSource>{};
        for (const auto& object : parsed.objects) {
            const auto previous =
                std::find_if(document_->objects.begin(), document_->objects.end(), [&](const auto& old) {
                    return old.id == object.id;
                });
            for (const auto visual : {true, false}) {
                const auto& path = visual ? object.visual : object.audio;
                if (path.has_value() == false ||
                    (previous != document_->objects.end() && path == (visual ? previous->visual : previous->audio))) {
                    continue;
                }
                resolve_(*path, false);
                if (source.has_value() == false) {
                    source.emplace(root_);
                }
                const auto expected = visual ? (parsed.game == "2d" ? "image" : "mesh") : "audio";
                const auto info = InspectAsset(*source, *path, expected);
                if (info.diagnostic.empty() == false) {
                    throw std::invalid_argument(object.id + (visual ? ".visual: " : ".audio: ") + *path + ": " +
                                                info.diagnostic);
                }
            }
        }
        const auto after = SerializeScene(parsed);
        if (before == after) {
            auto result = State();
            result.status = "NoChange";
            return result;
        }
        // 保存内容とは別に履歴を持ち、戻した後の新規編集だけがRedoを失効させる。
        auto next_history = history_;
        next_history.erase(next_history.begin() + static_cast<std::ptrdiff_t>(cursor_), next_history.end());
        next_history.push_back({NewId(), before, after});
        auto bytes = std::size_t{};
        for (const auto& item : next_history) {
            bytes += item.before.size() + item.after.size() + item.id.size();
        }
        while (next_history.size() > 256 || bytes > 8 * 1024 * 1024) {
            bytes -=
                next_history.front().before.size() + next_history.front().after.size() + next_history.front().id.size();
            next_history.pop_front();
        }
        document_ = std::move(parsed);
        history_ = std::move(next_history);
        cursor_ = history_.size();
        const auto previous = revision_++;
        auto result = State();
        result.before_revision = previous;
        result.changed_ids.assign(changed.begin(), changed.end());
        return result;
    }

    Result<DocumentInfo> SceneActions::Travel(const HistoryRequest& request, bool redo)
    {
        if ((redo && cursor_ == history_.size()) || (redo == false && cursor_ == 0)) {
            return std::unexpected(ActionError{"InvalidState", "history_id", "history is empty"});
        }
        const auto& item = history_[redo ? cursor_ : cursor_ - 1];
        if (item.id != request.history_id) {
            return std::unexpected(ActionError{"Conflict", "history_id", "next history item changed", revision_});
        }
        auto candidate = ParseScene(redo ? item.after : item.before);
        // 参照切れも保持し、読込み・履歴・復旧から修正できるようにする。
        document_ = std::move(candidate);
        if (redo) {
            ++cursor_;
        }
        else {
            --cursor_;
        }
        const auto previous = revision_++;
        auto result = State();
        result.before_revision = previous;
        return result;
    }

    Result<DocumentInfo> SceneActions::Find(const SceneQuery& request, bool properties)
    {
        if (document_.has_value() == false || request.document_id != document_->document_id) {
            return std::unexpected(ActionError{"NotFound", "document_id", "document is not open"});
        }
        const auto limit = request.limit.value_or(100);
        if (limit == 0 || limit > 1000) {
            throw std::invalid_argument("limit must be 1..1000");
        }
        auto offset = std::size_t{};
        if (request.cursor) {
            const auto separator = request.cursor->find(':');
            if (separator == std::string::npos) {
                throw std::invalid_argument("invalid cursor");
            }
            const auto prefix = std::to_string(revision_) + ":";
            if (request.cursor->starts_with(prefix) == false) {
                return std::unexpected(
                    ActionError{"Conflict", "cursor", "document changed during pagination", revision_});
            }
            const auto suffix = request.cursor->substr(separator + 1);
            if (suffix.empty() || suffix.find_first_not_of("0123456789") != suffix.npos) {
                throw std::invalid_argument("invalid cursor offset");
            }
            offset = std::stoull(suffix);
        }
        auto result = State();
        auto objects = Value::Array{};
        const auto all = std::get<Value::Array>(result.scene.at("objects").data);
        auto filtered = Value::Array{};
        for (const auto& value : all) {
            const auto& object = std::get<Value::Object>(value.data);
            if (request.object_id && std::get<std::string>(object.at("id").data) != *request.object_id) {
                continue;
            }
            if (request.kind && std::get<std::string>(object.at("kind").data) != *request.kind) {
                continue;
            }
            filtered.push_back(value);
        }
        if (properties && request.object_id && filtered.empty()) {
            return std::unexpected(ActionError{"NotFound", "object_id", "object does not exist"});
        }
        if (offset > filtered.size()) {
            throw std::invalid_argument("cursor offset exceeds object count");
        }
        const auto end = std::min(filtered.size(), offset + limit);
        objects.assign(filtered.begin() + offset, filtered.begin() + end);
        result.scene["objects"] = std::move(objects);
        if (end < filtered.size()) {
            result.next_cursor = std::to_string(revision_) + ":" + std::to_string(end);
        }
        return result;
    }

    std::filesystem::path SceneActions::RecoveryPath() const
    {
        return resolve_(".gengine/scene-recovery.json", true);
    }
    void SceneActions::ClearRecovery(bool discard_any)
    try {
        if (document_ && std::filesystem::exists(RecoveryPath())) {
            // 別文書の保存では復旧入力を消さない。切断時の書込みは最新一件に置き換える。
            if (discard_any == false) {
                const auto recovery = ParseJson(ReadFile(RecoveryPath(), 1024 * 1024));
                if (recovery.at("path") != path_ || recovery.at("scene").at("document_id") != document_->document_id) {
                    return;
                }
            }
            if (DeleteFileW(RecoveryPath().c_str()) == FALSE) {
                std::cerr << "cannot clear scene recovery input\n";
            }
        }
    } catch (const std::exception& error) {
        // 保存の確定後に復旧入力の整理が失敗しても、保存結果を失敗へ戻さない。
        if (discard_any) {
            throw;
        }
        std::cerr << "scene recovery cleanup failed: " << error.what() << '\n';
    }
    void SceneActions::RecoverOnDisconnect() noexcept
    {
        try {
            if (document_ && State().dirty) {
                const auto bytes =
                    Json{{"path", path_}, {"base_hash", disk_hash_}, {"scene", ParseJson(SerializeScene(*document_))}}
                        .dump(2);
                WriteAtomicFile(RecoveryPath(), bytes);
            }
        } catch (const std::exception& error) {
            std::cerr << "scene recovery failed: " << error.what() << '\n';
        }
    }
    Result<DocumentInfo> SceneActions::Recovery(const RecoveryRequest& request)
    {
        if (request.choice == "inspect") {
            auto state = State();
            if (state.recovery_available) {
                state.recovery =
                    std::get<Value::Object>(FromJson(ParseJson(ReadFile(RecoveryPath(), 1024 * 1024))).data);
            }
            return state;
        }
        if (request.choice == "discard") {
            ClearRecovery(true);
            return State();
        }
        if (request.choice != "restore") {
            throw std::invalid_argument("choose inspect, restore or discard");
        }
        if (State().dirty) {
            return std::unexpected(ActionError{"InvalidState", "choice", "save or reload current edits first"});
        }
        const auto input = ParseJson(ReadFile(RecoveryPath(), 1024 * 1024));
        if (input.at("path") != path_ || input.at("base_hash") != disk_hash_) {
            return std::unexpected(
                ActionError{"Conflict", "path", "recovery base differs from current file", revision_});
        }
        auto candidate = ParseScene(input.at("scene").dump());
        if (candidate.document_id != document_->document_id || candidate.game != document_->game) {
            throw std::invalid_argument("recovery document identity differs");
        }
        // 参照切れも保持し、読込み・履歴・復旧から修正できるようにする。
        document_ = std::move(candidate);
        history_.clear();
        cursor_ = 0;
        ++revision_;
        return State();
    }

    void SceneActions::Register(Registry& registry)
    {
        const auto readable = [](const auto&) -> std::optional<ActionError> {
            return {};
        };
        const auto checked = [this](const auto& r) {
            return Check(r.document_id, r.expected_revision);
        };
        registry.Add<EmptyRequest, DocumentInfo>("document.get_state", "編集文書と履歴を照会する", "read", false,
                                                 readable, [this](const auto&) {
                                                     return Guard([&] {
                                                         return State();
                                                     });
                                                 });
        registry.Add<DocumentOpen, DocumentInfo>("document.open", "登録済みシーンを開く", "document-control", true,
                                                 readable, [this](const auto& r) {
                                                     return Guard([&] {
                                                         return Open(r);
                                                     });
                                                 });
        registry.Add<DocumentReload, DocumentInfo>("document.reload", "シーンを再読込みする", "document-control", true,
                                                   checked, [this](const auto& r) {
                                                       return Guard([&] {
                                                           return Open({path_, r.expected_revision, r.choice});
                                                       });
                                                   });
        registry.Add<DocumentRequest, DocumentInfo>("document.save", "編集中のシーンを保存する", "file-output", true,
                                                    checked, [this](const auto&) {
                                                        return Guard([&] {
                                                            return Save();
                                                        });
                                                    });
        registry.Add<RecoveryRequest, DocumentInfo>("document.recover", "切断時の復旧入力を確認・復元・破棄する",
                                                    "document-control", true, checked, [this](const auto& r) {
                                                        return Guard([&] {
                                                            return Recovery(r);
                                                        });
                                                    });
        for (const auto properties : {false, true}) {
            registry.Add<SceneQuery, DocumentInfo>(properties ? "scene.get_properties" : "scene.find_objects",
                                                   "対象とプロパティを照会する", "read", false, readable,
                                                   [this, properties](const auto& r) {
                                                       return Guard([&] {
                                                           return Find(r, properties);
                                                       });
                                                   });
        }
        registry.Add<SceneCreate, DocumentInfo>("scene.create_object", "対象を追加する", "document-edit", true, checked,
                                                [this](const auto& r) {
                                                    return Guard([&] {
                                                        return Edit({{"create", {}, r.kind, r.properties}});
                                                    });
                                                });
        registry.Add<SceneSet, DocumentInfo>("scene.set_properties", "対象の値を確定する", "document-edit", true,
                                             checked, [this](const auto& r) {
                                                 return Guard([&] {
                                                     return Edit({{"set", r.object_id, {}, r.properties}});
                                                 });
                                             });
        registry.Add<SceneSettingsRequest, DocumentInfo>("scene.set_settings", "ゲーム設定を確定する", "document-edit",
                                                         true, checked, [this](const auto& r) {
                                                             return Guard([&] {
                                                                 return Edit({{"settings", {}, {}, r.properties}});
                                                             });
                                                         });
        registry.Add<SceneDelete, DocumentInfo>("scene.delete_objects", "対象を削除する", "document-edit", true,
                                                checked, [this](const auto& r) {
                                                    return Guard([&] {
                                                        auto edits = std::vector<SceneEdit>{};
                                                        for (const auto& id : r.object_ids) {
                                                            edits.push_back({"delete", id, {}, {}});
                                                        }
                                                        return Edit(edits);
                                                    });
                                                });
        registry.Add<SceneBatch, DocumentInfo>("scene.apply_batch", "複数編集を一度に確定する", "document-edit", true,
                                               checked, [this](const auto& r) {
                                                   return Guard([&] {
                                                       return Edit(r.edits);
                                                   });
                                               });
        for (const auto redo : {false, true}) {
            registry.Add<HistoryRequest, DocumentInfo>(redo ? "history.redo" : "history.undo", "編集履歴を移動する",
                                                       "document-edit", true, checked, [this, redo](const auto& r) {
                                                           return Guard([&] {
                                                               return Travel(r, redo);
                                                           });
                                                       });
        }
    }
} // namespace gengine::editor
