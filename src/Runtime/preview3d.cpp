#include "preview3d.hpp"
#include <algorithm>
#include <cmath>

namespace gengine
{
    PreviewGeometry3d BuildPreview3d(const SceneDocument& scene, const Mapping3d& mapping, std::string_view selected)
    {
        auto geometry = PreviewGeometry3d{};
        const auto line = [&](Point3d first, Point3d second, Point3d color) {
            for (const auto& point : {first, second}) {
                auto& vertex = geometry.lines.at(geometry.line_count++);
                for (auto axis = 0u; axis < 3; ++axis) {
                    vertex.position[axis] = float(point[axis]);
                    vertex.color[axis] = float(color[axis]);
                }
                vertex.color[3] = 1;
            }
        };
        // 三次元の移動範囲を12辺で示す。ゲーム用の地面グリッドとは分ける。
        auto corners = std::array<Point3d, 8>{};
        for (auto index = 0u; index < 8; ++index) {
            for (auto axis = 0u; axis < 3; ++axis) {
                corners[index][axis] =
                    (index & (1u << axis)) ? scene.settings.bounds_max[axis] : scene.settings.bounds_min[axis];
            }
        }
        for (auto index = 0u; index < 8; ++index) {
            for (auto axis = 0u; axis < 3; ++axis) {
                if ((index & (1u << axis)) == 0) {
                    line(corners[index], corners[index | (1u << axis)], {.2, .35, .4});
                }
            }
        }
        // ピクセルの大きさを16:9の重ね描きへ変換し、ズームにかかわらずハンドルを保つ。
        const auto unit = 9 / mapping.area.height;
        const auto rectangle = [&](Point2d center, double half_width, double half_height, Point3d color,
                                   double angle = 0) {
            auto item =
                rengine::Sprite(0, 0, float(half_width * 2 * unit), float(half_height * 2 * unit), float(angle));
            item.world[12] = float((center[0] - mapping.area.left) * unit) - (item.world[0] + item.world[4]) * .5f;
            item.world[13] = float((center[1] - mapping.area.top) * unit) - (item.world[1] + item.world[5]) * .5f;
            for (auto axis = 0u; axis < 3; ++axis) {
                item.color[axis] = float(color[axis]);
            }
            geometry.overlays.at(geometry.overlay_count++) = item;
        };
        for (const auto& object : scene.objects) {
            const auto position = Point3d{object.position[0], object.position[1], object.position[2]};
            const auto center = mapping.ToPixel(position);
            if (object.kind == "game_camera") {
                for (const auto& edge : CameraLines3d(object)) {
                    line(edge.first, edge.second, object.id == selected ? Point3d{1, .9, .2} : Point3d{.5, .7, .3});
                }
            }
            if (object.id != selected || mapping.Visible(position) == false) {
                continue;
            }
            const auto radius = object.kind == "enemy_spawn" ? .75 : .25;
            const auto size = object.kind == "game_camera"
                                  ? 7.0
                                  : std::max(6.0, radius / mapping.view.height * mapping.area.height + 3);
            rectangle({center[0], center[1] - size}, size, 1, {1, .9, .2});
            rectangle({center[0], center[1] + size}, size, 1, {1, .9, .2});
            rectangle({center[0] - size, center[1]}, 1, size, {1, .9, .2});
            rectangle({center[0] + size, center[1]}, 1, size, {1, .9, .2});
            if (object.kind == "game_camera") {
                continue;
            }
            const auto axes = Axes3d(mapping, position);
            const auto colors = std::array<Point3d, 3>{{{1, .3, .2}, {.2, 1, .3}, {.3, .6, 1}}};
            for (auto axis = 0u; axis < 3; ++axis) {
                if (axes[axis].visible == false) {
                    continue;
                }
                const auto& direction = axes[axis].direction;
                const auto angle = std::atan2(direction[1], direction[0]);
                rectangle({center[0] + direction[0] * 44, center[1] + direction[1] * 44}, 16, 2, colors[axis], angle);
                rectangle({center[0] + direction[0] * 60, center[1] + direction[1] * 60}, 4, 5, colors[axis], angle);
            }
        }
        return geometry;
    }
} // namespace gengine
