#include "view3d.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace gengine
{
    Point3d Add3d(Point3d a, Point3d b)
    {
        return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
    }
    Point3d Subtract3d(Point3d a, Point3d b)
    {
        return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    }
    Point3d Multiply3d(Point3d point, double factor)
    {
        return {point[0] * factor, point[1] * factor, point[2] * factor};
    }
    double Dot3d(Point3d a, Point3d b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }
    Point3d Cross3d(Point3d a, Point3d b)
    {
        return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    }
    Point3d Unit3d(Point3d point)
    {
        const auto length = std::hypot(point[0], point[1], point[2]);
        if (std::isfinite(length) == false || length == 0) {
            throw std::invalid_argument("direction must have a finite nonzero length");
        }
        return Multiply3d(point, 1 / length);
    }
    void ValidateView3d(const View3d& view)
    {
        if (std::all_of(view.center.begin(), view.center.end(),
                        [](double value) {
                            return std::isfinite(value) && std::abs(value) <= 1e30;
                        }) == false ||
            std::isfinite(view.height) == false || view.height < 1e-6 || view.height > 1e30 ||
            std::isfinite(view.yaw_degrees) == false || std::abs(view.yaw_degrees) > 1e6 ||
            std::isfinite(view.pitch_degrees) == false || std::abs(view.pitch_degrees) > 85) {
            throw std::invalid_argument(
                "3D view: finite center within +/-1e30, yaw within +/-1e6, height 1e-6..1e30, pitch -85..85 required");
        }
    }
    Mapping3d MakeMapping3d(View3d view, double width, double height)
    {
        ValidateView3d(view);
        const auto yaw = view.yaw_degrees * std::numbers::pi / 180;
        const auto pitch = view.pitch_degrees * std::numbers::pi / 180;
        const auto back = Point3d{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        const auto right = Point3d{std::cos(yaw), 0, -std::sin(yaw)};
        return {view, MakeMapping2d({{}, view.height}, width, height), right, Cross3d(back, right), back};
    }
    Point2d Mapping3d::ToPixel(Point3d world) const
    {
        const auto relative = Subtract3d(world, view.center);
        return area.ToPixel({Dot3d(relative, right), Dot3d(relative, up)});
    }
    Point3d Mapping3d::OnViewPlane(Point2d pixel) const
    {
        const auto plane = area.ToWorld(pixel);
        return Add3d(view.center, Add3d(Multiply3d(right, plane[0]), Multiply3d(up, plane[1])));
    }
    double Mapping3d::Distance() const
    {
        return std::max(100.0, view.height * 4);
    }
    Ray3d Mapping3d::Ray(Point2d pixel) const
    {
        return {Add3d(OnViewPlane(pixel), Multiply3d(back, Distance())), Multiply3d(back, -1)};
    }
    bool Mapping3d::Visible(Point3d world) const
    {
        return area.Contains(ToPixel(world)) && std::abs(Dot3d(Subtract3d(world, view.center), back)) <= Distance();
    }
    View3d FitScene3d(const SceneDocument& scene)
    {
        ValidateScene(scene);
        if (scene.game != "3d") {
            throw std::invalid_argument("3D view requires a 3D scene");
        }
        auto low = Point3d{scene.settings.bounds_min[0], scene.settings.bounds_min[1], scene.settings.bounds_min[2]};
        auto high = Point3d{scene.settings.bounds_max[0], scene.settings.bounds_max[1], scene.settings.bounds_max[2]};
        for (const auto& object : scene.objects) {
            for (auto axis = 0u; axis < 3; ++axis) {
                low[axis] = std::min(low[axis], double(object.position[axis]) - 1);
                high[axis] = std::max(high[axis], double(object.position[axis]) + 1);
            }
            if (object.kind == "game_camera") {
                for (const auto& edge : CameraLines3d(object)) {
                    for (auto axis = 0u; axis < 3; ++axis) {
                        low[axis] = std::min({low[axis], edge.first[axis], edge.second[axis]});
                        high[axis] = std::max({high[axis], edge.first[axis], edge.second[axis]});
                    }
                }
            }
        }
        // 外接球を収める高さにして、どの向きから見ても移動範囲が欠けないようにする。
        const auto diagonal = Subtract3d(high, low);
        auto center = Multiply3d(Add3d(low, high), .5);
        for (auto& value : center) {
            value = std::clamp(value, -1e30, 1e30);
        }
        return {center, std::clamp(std::sqrt(Dot3d(diagonal, diagonal)) * 1.15, 1.0, 1e30), 45, 30};
    }
    View3d ZoomView3d(const Mapping3d& mapping, Point2d anchor, double factor)
    {
        if (std::isfinite(factor) == false || factor <= 0) {
            throw std::invalid_argument("zoom factor must be positive and finite");
        }
        const auto world = mapping.OnViewPlane(anchor);
        auto view = mapping.view;
        view.height *= factor;
        view.center = Add3d(world, Multiply3d(Subtract3d(view.center, world), factor));
        ValidateView3d(view);
        return view;
    }
    void EditorCamera3d(float* view_projection, const Mapping3d& mapping)
    {
        // 基底はdoubleで計算済みなので、大きな中心をfloatのlook-atで正規化し直さない。
        // rEngineの行ベクトル規約に合わせ、中心をclip空間の(0,0,0.5)へ写す。
        std::fill_n(view_projection, 16, 0.0f);
        const auto horizontal = 2 / (mapping.view.height * 16 / 9);
        const auto vertical = 2 / mapping.view.height;
        const auto depth = 1 / (mapping.Distance() * 2);
        for (auto axis = 0u; axis < 3; ++axis) {
            view_projection[axis * 4] = float(mapping.right[axis] * horizontal);
            view_projection[axis * 4 + 1] = float(mapping.up[axis] * vertical);
            view_projection[axis * 4 + 2] = float(-mapping.back[axis] * depth);
        }
        view_projection[12] = float(-Dot3d(mapping.view.center, mapping.right) * horizontal);
        view_projection[13] = float(-Dot3d(mapping.view.center, mapping.up) * vertical);
        view_projection[14] = float(.5 + Dot3d(mapping.view.center, mapping.back) * depth);
        view_projection[15] = 1;
    }
    std::array<Axis3d, 3> Axes3d(const Mapping3d& mapping, Point3d position)
    {
        const auto center = mapping.ToPixel(position);
        const auto scale = mapping.area.height / mapping.view.height;
        auto axes = std::array<Axis3d, 3>{};
        for (auto axis = 0u; axis < 3; ++axis) {
            auto& item = axes[axis];
            const auto length = std::hypot(mapping.right[axis], mapping.up[axis]);
            item.visible = length >= .15 && mapping.Visible(position);
            item.projected_unit = {mapping.right[axis] * scale, -mapping.up[axis] * scale};
            if (item.visible) {
                item.direction = {mapping.right[axis] / length, -mapping.up[axis] / length};
                const auto end = Point2d{center[0] + item.direction[0] * 66, center[1] + item.direction[1] * 66};
                if (mapping.area.Contains(end) == false) {
                    item.direction[0] *= -1;
                    item.direction[1] *= -1;
                }
            }
        }
        return axes;
    }
    std::array<Line3d, 9> CameraLines3d(const SceneObject& camera)
    {
        if (camera.kind != "game_camera" || camera.position.size() != 3 || camera.target.size() != 3 ||
            std::isfinite(camera.fov_degrees) == false || camera.fov_degrees <= 0 || camera.fov_degrees >= 180) {
            throw std::invalid_argument("camera outline requires a validated 3D camera");
        }
        const auto origin = Point3d{camera.position[0], camera.position[1], camera.position[2]};
        const auto target = Point3d{camera.target[0], camera.target[1], camera.target[2]};
        const auto forward = Unit3d(Subtract3d(target, origin));
        const auto right = Unit3d(Cross3d(forward, {0, 1, 0}));
        const auto up = Cross3d(right, forward);
        const auto center = Add3d(origin, Multiply3d(forward, 2));
        const auto half_height = std::tan(camera.fov_degrees * std::numbers::pi / 360) * 2;
        auto corners = std::array<Point3d, 4>{};
        const auto signs = std::array<Point2d, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
        for (auto i = 0u; i < 4; ++i) {
            corners[i] = Add3d(center, Add3d(Multiply3d(right, signs[i][0] * half_height * 16 / 9),
                                             Multiply3d(up, signs[i][1] * half_height)));
        }
        auto lines = std::array<Line3d, 9>{};
        for (auto i = 0u; i < 4; ++i) {
            lines[i] = {origin, corners[i]};
            lines[4 + i] = {corners[i], corners[(i + 1) % 4]};
        }
        lines[8] = {origin, target};
        return lines;
    }
} // namespace gengine
