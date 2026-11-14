#include "picking3d.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace gengine::editor
{
    namespace
    {
        double SegmentDistance(Point2d point, Point2d first, Point2d second)
        {
            const auto dx = second[0] - first[0];
            const auto dy = second[1] - first[1];
            const auto length_squared = dx * dx + dy * dy;
            const auto t =
                length_squared == 0
                    ? 0
                    : std::clamp(((point[0] - first[0]) * dx + (point[1] - first[1]) * dy) / length_squared, 0.0, 1.0);
            return std::hypot(point[0] - first[0] - t * dx, point[1] - first[1] - t * dy);
        }
    } // namespace

    std::optional<unsigned> HitAxis3d(const Mapping3d& mapping, Point3d position, Point2d pixel)
    {
        if (mapping.area.Contains(pixel) == false) {
            return {};
        }
        const auto center = mapping.ToPixel(position);
        const auto axes = Axes3d(mapping, position);
        auto picked = std::optional<unsigned>{};
        auto distance = 7.0;
        for (auto axis = 0u; axis < 3; ++axis) {
            if (axes[axis].visible == false) {
                continue;
            }
            const auto& d = axes[axis].direction;
            const auto candidate = SegmentDistance(pixel, {center[0] + d[0] * 28, center[1] + d[1] * 28},
                                                   {center[0] + d[0] * 60, center[1] + d[1] * 60});
            // 重なった軸では、同距離なら後から描く軸を優先する。
            if (candidate <= distance) {
                picked = axis;
                distance = candidate;
            }
        }
        return picked;
    }

    std::string PickScene3d(const SceneDocument& scene, const Mapping3d& mapping, Point2d pixel)
    {
        if (mapping.area.Contains(pixel) == false) {
            return {};
        }
        const auto ray = mapping.Ray(pixel);
        auto selected = std::string{};
        auto nearest = std::numeric_limits<double>::infinity();
        // 同じ画素に投影された球でも、レイの手前にある対象を選ぶ。
        for (const auto& object : scene.objects) {
            if (object.kind == "game_camera") {
                continue;
            }
            const auto offset = Subtract3d({object.position[0], object.position[1], object.position[2]}, ray.origin);
            const auto along = Dot3d(offset, ray.direction);
            const auto perpendicular = Subtract3d(offset, Multiply3d(ray.direction, along));
            const auto radius = object.kind == "enemy_spawn" ? .75 : .25;
            const auto remainder = radius * radius - Dot3d(perpendicular, perpendicular);
            if (remainder < 0) {
                continue;
            }
            const auto near = along - std::sqrt(remainder);
            const auto far = along + std::sqrt(remainder);
            const auto depth = near >= 0 ? near : far;
            if (depth >= 0 && depth <= mapping.Distance() * 2 && depth <= nearest) {
                nearest = depth;
                selected = object.id;
            }
        }
        if (selected.empty() == false) {
            return selected;
        }
        // カメラは保存される位置・向きの線を選べる。球本体より優先しない。
        for (const auto& object : scene.objects) {
            if (object.kind != "game_camera") {
                continue;
            }
            for (const auto& edge : CameraLines3d(object)) {
                // GPUの近面・遠面で消える部分をクリック対象に残さない。
                const auto first_depth = Dot3d(Subtract3d(edge.first, mapping.view.center), mapping.back);
                const auto delta = Dot3d(Subtract3d(edge.second, edge.first), mapping.back);
                auto low = 0.0;
                auto high = 1.0;
                if (delta == 0) {
                    if (std::abs(first_depth) > mapping.Distance()) {
                        continue;
                    }
                }
                else {
                    const auto a = (-mapping.Distance() - first_depth) / delta;
                    const auto b = (mapping.Distance() - first_depth) / delta;
                    low = std::max(0.0, std::min(a, b));
                    high = std::min(1.0, std::max(a, b));
                    if (low > high) {
                        continue;
                    }
                }
                const auto direction = Subtract3d(edge.second, edge.first);
                const auto first = Add3d(edge.first, Multiply3d(direction, low));
                const auto second = Add3d(edge.first, Multiply3d(direction, high));
                if (SegmentDistance(pixel, mapping.ToPixel(first), mapping.ToPixel(second)) <= 6) {
                    return object.id;
                }
            }
        }
        return {};
    }

    std::optional<Point3d> PlacementPoint3d(const SceneDocument& scene, const Mapping3d& mapping, Point2d pixel)
    {
        if (mapping.area.Contains(pixel) == false) {
            return {};
        }
        const auto ray = mapping.Ray(pixel);
        if (std::abs(ray.direction[1]) < 1e-6) {
            return {};
        }
        const auto y = std::clamp(2.0, double(scene.settings.bounds_min[1]), double(scene.settings.bounds_max[1]));
        const auto distance = (y - ray.origin[1]) / ray.direction[1];
        if (distance < 0 || distance > mapping.Distance() * 2) {
            return {};
        }
        auto point = Add3d(ray.origin, Multiply3d(ray.direction, distance));
        point[1] = y;
        for (auto axis = 0u; axis < 3; ++axis) {
            if (point[axis] < scene.settings.bounds_min[axis] || point[axis] > scene.settings.bounds_max[axis]) {
                return {};
            }
        }
        return point;
    }
} // namespace gengine::editor
