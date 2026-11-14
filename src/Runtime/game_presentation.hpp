#pragma once
#include "assets.hpp"
#include "visual_manifest.hpp"
#include <memory>
#include <rengine/renderer.hpp>

namespace gengine
{
    // ゲームが指定する小さな表示素材を一度だけ読み、背景と戦闘で共用する。
    // 判定・移動・ゲーム固有の個体IDは所有しない。
    class GamePresentation final
    {
      public:
        GamePresentation(rengine::Renderer& renderer, const AssetSource& source, AssetMetrics& metrics);
        ~GamePresentation();
        const VisualManifest& definition() const;
        rengine::Material atlas_material() const;
        rengine::Material detail_material() const;
        bool HasLogo() const;
        rengine::Draw Backdrop(bool gameplay, std::uint32_t scene = 0);
        rengine::Draw Logo(float x, float y, float width);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace gengine
