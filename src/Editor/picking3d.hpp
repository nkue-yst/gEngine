#pragma once
#include "view3d.hpp"
#include <optional>

namespace gengine::editor
{
    // ハンドルは画面上の太さ、対象本体はレイと球の距離で判定する。
    std::optional<unsigned> HitAxis3d(const Mapping3d& mapping, Point3d position, Point2d pixel);
    std::string PickScene3d(const SceneDocument& scene, const Mapping3d& mapping, Point2d pixel);
    // 追加の高さは既定のY=2を移動範囲へ収める。平行な視線と範囲外は配置できない。
    std::optional<Point3d> PlacementPoint3d(const SceneDocument& scene, const Mapping3d& mapping, Point2d pixel);
} // namespace gengine::editor
