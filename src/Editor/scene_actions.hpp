#pragma once
#include "scene_document.hpp"
#include "vm_actions.hpp"
#include <deque>
#include <filesystem>

namespace gengine::editor
{
    struct DocumentRequest
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
    };
    template <> struct Record<DocumentRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &DocumentRequest::document_id),
                              MakeField("expected_revision", &DocumentRequest::expected_revision)};
        }
    };
    struct DocumentOpen
    {
        std::string path;
        std::optional<std::uint64_t> expected_revision;
        std::optional<std::string> choice;
    };
    template <> struct Record<DocumentOpen>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("path", &DocumentOpen::path),
                              MakeField("expected_revision", &DocumentOpen::expected_revision),
                              MakeField("choice", &DocumentOpen::choice)};
        }
    };
    struct DocumentReload
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        std::optional<std::string> choice;
    };
    template <> struct Record<DocumentReload>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &DocumentReload::document_id),
                              MakeField("expected_revision", &DocumentReload::expected_revision),
                              MakeField("choice", &DocumentReload::choice)};
        }
    };
    struct SceneCreate
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        std::string kind;
        std::optional<Value::Object> properties;
    };
    template <> struct Record<SceneCreate>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &SceneCreate::document_id),
                              MakeField("expected_revision", &SceneCreate::expected_revision),
                              MakeField("kind", &SceneCreate::kind), MakeField("properties", &SceneCreate::properties)};
        }
    };
    struct SceneSet
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        std::string object_id;
        Value::Object properties;
    };
    template <> struct Record<SceneSet>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &SceneSet::document_id),
                              MakeField("expected_revision", &SceneSet::expected_revision),
                              MakeField("object_id", &SceneSet::object_id),
                              MakeField("properties", &SceneSet::properties)};
        }
    };
    struct SceneSettingsRequest
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        Value::Object properties;
    };
    template <> struct Record<SceneSettingsRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &SceneSettingsRequest::document_id),
                              MakeField("expected_revision", &SceneSettingsRequest::expected_revision),
                              MakeField("properties", &SceneSettingsRequest::properties)};
        }
    };
    struct SceneDelete
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        std::vector<std::string> object_ids;
    };
    template <> struct Record<SceneDelete>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &SceneDelete::document_id),
                              MakeField("expected_revision", &SceneDelete::expected_revision),
                              MakeField("object_ids", &SceneDelete::object_ids)};
        }
    };
    struct SceneQuery
    {
        std::string document_id;
        std::optional<std::string> object_id;
        std::optional<std::string> kind;
        std::optional<std::uint32_t> limit;
        std::optional<std::string> cursor;
    };
    template <> struct Record<SceneQuery>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &SceneQuery::document_id),
                              MakeField("object_id", &SceneQuery::object_id), MakeField("kind", &SceneQuery::kind),
                              MakeField("limit", &SceneQuery::limit), MakeField("cursor", &SceneQuery::cursor)};
        }
    };
    struct SceneEdit
    {
        std::string edit;
        std::optional<std::string> object_id;
        std::optional<std::string> kind;
        std::optional<Value::Object> properties;
    };
    template <> struct Record<SceneEdit>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("edit", &SceneEdit::edit), MakeField("object_id", &SceneEdit::object_id),
                              MakeField("kind", &SceneEdit::kind), MakeField("properties", &SceneEdit::properties)};
        }
    };
    struct SceneBatch
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        std::vector<SceneEdit> edits;
    };
    template <> struct Record<SceneBatch>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &SceneBatch::document_id),
                              MakeField("expected_revision", &SceneBatch::expected_revision),
                              MakeField("edits", &SceneBatch::edits)};
        }
    };
    struct HistoryRequest
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        std::string history_id;
    };
    template <> struct Record<HistoryRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &HistoryRequest::document_id),
                              MakeField("expected_revision", &HistoryRequest::expected_revision),
                              MakeField("history_id", &HistoryRequest::history_id)};
        }
    };
    struct RecoveryRequest
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        std::string choice;
    };
    template <> struct Record<RecoveryRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &RecoveryRequest::document_id),
                              MakeField("expected_revision", &RecoveryRequest::expected_revision),
                              MakeField("choice", &RecoveryRequest::choice)};
        }
    };
    struct DocumentInfo
    {
        std::string status = "Succeeded";
        std::string document_id;
        std::string path;
        std::uint64_t revision = 0;
        std::uint64_t before_revision = 0;
        bool dirty = false;
        std::optional<std::string> undo_id;
        std::optional<std::string> redo_id;
        Value::Object scene;
        std::vector<std::string> changed_ids;
        std::optional<std::string> next_cursor;
        bool recovery_available = false;
        std::optional<Value::Object> recovery;
    };
    template <> struct Record<DocumentInfo>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("status", &DocumentInfo::status),
                              MakeField("document_id", &DocumentInfo::document_id),
                              MakeField("path", &DocumentInfo::path),
                              MakeField("revision", &DocumentInfo::revision),
                              MakeField("before_revision", &DocumentInfo::before_revision),
                              MakeField("dirty", &DocumentInfo::dirty),
                              MakeField("undo_id", &DocumentInfo::undo_id),
                              MakeField("redo_id", &DocumentInfo::redo_id),
                              MakeField("scene", &DocumentInfo::scene),
                              MakeField("changed_ids", &DocumentInfo::changed_ids),
                              MakeField("next_cursor", &DocumentInfo::next_cursor),
                              MakeField("recovery_available", &DocumentInfo::recovery_available),
                              MakeField("recovery", &DocumentInfo::recovery)};
        }
    };

    // GUIとMCPはこの所有スレッド上の文書を共用する。候補の検証後だけ状態を確定する。
    class SceneActions
    {
      public:
        using Resolver = std::function<std::filesystem::path(std::string_view, bool)>;
        void Register(Registry& registry);
        void Attach(std::filesystem::path root, std::vector<std::string> paths, Resolver resolver,
                    std::function<bool()> busy);
        void Detach();
        DocumentInfo State() const;
        std::uint64_t Revision() const
        {
            return revision_;
        }
        const std::string& Path() const
        {
            return path_;
        }
        const SceneDocument* document() const
        {
            return document_ ? &*document_ : nullptr;
        }
        std::optional<ActionError> Check(std::string_view id, std::uint64_t revision, bool writing = true) const;
        Result<DocumentInfo> ResolveUnsaved(std::optional<std::string> choice,
                                            std::optional<std::uint64_t> expected_revision);
        void RecoverOnDisconnect() noexcept;

      private:
        struct History
        {
            std::string id;
            std::string before;
            std::string after;
        };
        Result<DocumentInfo> Open(const DocumentOpen& request);
        Result<DocumentInfo> Save();
        Result<DocumentInfo> Edit(const std::vector<SceneEdit>& edits);
        Result<DocumentInfo> Travel(const HistoryRequest& request, bool redo);
        Result<DocumentInfo> Recovery(const RecoveryRequest& request);
        Result<DocumentInfo> Find(const SceneQuery& request, bool properties);
        std::filesystem::path RecoveryPath() const;
        void ClearRecovery(bool discard_any = false);
        std::filesystem::path root_;
        std::vector<std::string> paths_;
        Resolver resolve_;
        std::function<bool()> busy_;
        std::optional<SceneDocument> document_;
        std::string path_;
        std::string saved_;
        std::string disk_hash_;
        std::uint64_t revision_ = 0;
        std::deque<History> history_;
        std::size_t cursor_ = 0;
    };
} // namespace gengine::editor
