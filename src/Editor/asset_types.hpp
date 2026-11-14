#pragma once
#include "action_types.hpp"
#include "assets.hpp"

namespace gengine::editor
{
    template <> struct Record<AssetInfo>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("path", &AssetInfo::path), MakeField("kind", &AssetInfo::kind),
                              MakeField("bytes", &AssetInfo::bytes), MakeField("summary", &AssetInfo::summary),
                              MakeField("diagnostic", &AssetInfo::diagnostic)};
        }
    };
    template <> struct Record<AssetReference>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("object_id", &AssetReference::object_id),
                              MakeField("slot", &AssetReference::slot), MakeField("path", &AssetReference::path),
                              MakeField("diagnostic", &AssetReference::diagnostic)};
        }
    };
    struct AssetCatalog
    {
        std::vector<AssetInfo> entries;
        std::vector<AssetReference> references;
    };
    template <> struct Record<AssetCatalog>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("entries", &AssetCatalog::entries),
                              MakeField("references", &AssetCatalog::references)};
        }
    };
    struct AssetRegister
    {
        std::string project_id;
        std::uint64_t expected_revision = 0;
        std::string path;
    };
    template <> struct Record<AssetRegister>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("project_id", &AssetRegister::project_id),
                              MakeField("expected_revision", &AssetRegister::expected_revision),
                              MakeField("path", &AssetRegister::path)};
        }
    };
} // namespace gengine::editor
