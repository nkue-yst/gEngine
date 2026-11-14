#include "editor_core.hpp"
#include "json.hpp"
#include <algorithm>

namespace gengine::editor
{
    AssetCatalog EditorCore::ListAssets() const
    {
        auto result = AssetCatalog{};
        const auto source = AssetSource(AllowedPath(project_->path).parent_path());
        for (const auto& path : project_->assets) {
            auto info = AssetInfo{};
            try {
                ProjectPath(path);
                info = InspectAsset(source, path);
            } catch (const std::exception& error) {
                info.path = path;
                info.diagnostic = error.what();
            }
            result.entries.push_back(std::move(info));
        }
        if (const auto* scene = scenes_.document()) {
            result.references = InspectSceneAssets(*scene, source);
        }
        return result;
    }

    void EditorCore::RegisterAssets(Registry& registry)
    {
        registry.Add<EmptyRequest, AssetCatalog>(
            "asset.list", "登録した素材を再検証して一覧を取得する", "read", false,
            [this](const EmptyRequest&) -> std::optional<ActionError> {
                if (project_.has_value() == false) {
                    return ActionError{"InvalidState", {}, "open a project first"};
                }
                return {};
            },
            [this](const EmptyRequest&) -> Result<AssetCatalog> {
                try {
                    asset_catalog_ = ListAssets();
                    ++asset_catalog_revision_;
                    preview_assets_dirty_ = true;
                    return asset_catalog_;
                } catch (const std::exception& error) {
                    return std::unexpected(ActionError{"IoError", "path", error.what()});
                }
            });
        registry.Add<AssetRegister, EditorState>(
            "asset.register", "プロジェクト内の対応素材を検証して登録する", "file-output", true,
            [this](const AssetRegister& request) {
                return CheckProject(request.project_id, request.expected_revision);
            },
            [this](const AssetRegister& request) -> Result<EditorState> {
                try {
                    // 原本に触れず、形式まで検証した論理パスだけを設定へ追加する。
                    ProjectPath(request.path);
                    const auto source = AssetSource(AllowedPath(project_->path).parent_path());
                    const auto info = InspectAsset(source, request.path);
                    if (info.diagnostic.empty() == false) {
                        return std::unexpected(ActionError{"InvalidArgument", "path", info.diagnostic});
                    }
                    for (const auto& path : project_->assets) {
                        if (_wcsicmp(Wide(path).c_str(), Wide(request.path).c_str()) == 0) {
                            return State();
                        }
                    }
                    if (project_->assets.size() >= 256) {
                        return std::unexpected(ActionError{"CapacityExceeded", "assets", "at most 256 assets"});
                    }

                    // 外部変更と書込み失敗では、登録一覧もrevisionも変更しない。
                    const auto path = AllowedPath(project_->path);
                    const auto original = ReadFile(path, 64 * 1024);
                    if (Sha256(original) != project_hash_) {
                        return std::unexpected(ActionError{"Conflict", "path", "project changed outside Editor"});
                    }
                    auto document = Decode<ProjectFile>(FromJson(ParseJson(original, 64 * 1024))).value();
                    auto next = project_->assets;
                    next.push_back(request.path);
                    document.assets = next;
                    const auto bytes = AsJson(Encode(document)).dump(2) + "\n";
                    if (bytes.size() > 64 * 1024) {
                        return std::unexpected(ActionError{"CapacityExceeded", "assets", "project exceeds 64 KiB"});
                    }
                    WriteAtomicFile(path, bytes);
                    project_->assets = std::move(next);
                    project_->revision = ++revision_;
                    project_hash_ = Sha256(bytes);
                    return State();
                } catch (const std::exception& error) {
                    return std::unexpected(ActionError{"IoError", "path", error.what()});
                }
            });
    }
} // namespace gengine::editor
