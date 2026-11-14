#pragma once
#include "view3d.hpp"
#include <rengine/renderer.hpp>

namespace gengine
{
    // 編集表示に必要な線と画面上の矩形。固定容量なのでドラッグ中にGPU資源を作らない。
    struct PreviewGeometry3d
    {
        std::array<rengine::Vertex, 96> lines{};
        std::uint32_t line_count = 0;
        std::array<rengine::Instance, 24> overlays{};
        std::uint32_t overlay_count = 0;
    };
    PreviewGeometry3d BuildPreview3d(const SceneDocument& scene, const Mapping3d& mapping, std::string_view selected);
} // namespace gengine
