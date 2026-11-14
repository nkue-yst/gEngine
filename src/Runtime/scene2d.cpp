#include "scene2d.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gengine
{
    Point2d HandleDirection2d(const Mapping2d& mapping, Point2d center)
    {
        // 端に近い対象でも操作できるよう、余裕のある側へハンドルを向ける。
        return {center[0] + 66 <= mapping.left + mapping.width ? 1.0 : -1.0,
                center[1] - 66 >= mapping.top ? 1.0 : -1.0};
    }

    Handle2d HitHandle2d(Point2d center, Point2d pixel, Point2d direction)
    {
        const auto x = (pixel[0] - center[0]) * direction[0];
        const auto y = (center[1] - pixel[1]) * direction[1];
        if (x >= 8 && x <= 24 && y >= 8 && y <= 24) {
            return Handle2d::kPlane;
        }
        if (x >= 28 && x <= 64 && std::abs(y) <= 7) {
            return Handle2d::kX;
        }
        if (y >= 28 && y <= 64 && std::abs(x) <= 7) {
            return Handle2d::kY;
        }
        return Handle2d::kNone;
    }

    std::string PickScene2d(const SceneDocument& scene, const Mapping2d& mapping, Point2d pixel)
    {
        if (mapping.Contains(pixel) == false) {
            return {};
        }
        const auto world = mapping.ToWorld(pixel);
        // 敵を後に描くため、重なりでは敵を先に拾う。
        for (const auto kind : {"enemy_spawn", "player_spawn"}) {
            for (const auto& object : scene.objects) {
                if (object.kind != kind) {
                    continue;
                }
                const auto dx = std::abs(world[0] - object.position[0]);
                const auto dy = std::abs(world[1] - object.position[1]);
                const auto hit =
                    object.kind == "enemy_spawn" ? dx <= .75 && dy <= .55 : dx + dy <= .18 * std::sqrt(2.0);
                if (hit) {
                    return object.id;
                }
            }
        }
        for (const auto& object : scene.objects) {
            if (object.kind == "game_camera") {
                const auto x = std::abs(world[0]);
                const auto y = std::abs(world[1]);
                const auto w = double(object.view_size[0]) / 2;
                const auto h = double(object.view_size[1]) / 2;
                const auto tolerance = mapping.view.height / mapping.height * 5;
                if ((std::abs(x - w) <= tolerance && y <= h + tolerance) ||
                    (std::abs(y - h) <= tolerance && x <= w + tolerance)) {
                    return object.id;
                }
            }
        }
        return {};
    }
    void ValidateView2d(const View2d& view)
    {
        if (std::isfinite(view.center[0]) == false || std::isfinite(view.center[1]) == false ||
            std::isfinite(view.height) == false || view.height <= 0) {
            throw std::invalid_argument("view requires finite center and positive finite height");
        }
    }

    Mapping2d MakeMapping2d(View2d view, double width, double height)
    {
        ValidateView2d(view);
        if (std::isfinite(width) == false || std::isfinite(height) == false || width <= 0 || height <= 0) {
            throw std::invalid_argument("viewport dimensions must be positive and finite");
        }
        const auto scale = std::min(width / 16, height / 9);
        return {view, (width - scale * 16) / 2, (height - scale * 9) / 2, scale * 16, scale * 9};
    }

    bool Mapping2d::Contains(Point2d pixel) const
    {
        return pixel[0] >= left && pixel[0] < left + width && pixel[1] >= top && pixel[1] < top + height;
    }

    Point2d Mapping2d::ToWorld(Point2d pixel) const
    {
        // 画面の下向きYを、ゲームの上向きYへ反転する。
        return {view.center[0] + (pixel[0] - left - width / 2) * view.height / height,
                view.center[1] - (pixel[1] - top - height / 2) * view.height / height};
    }

    Point2d Mapping2d::ToPixel(Point2d world) const
    {
        return {left + width / 2 + (world[0] - view.center[0]) * height / view.height,
                top + height / 2 - (world[1] - view.center[1]) * height / view.height};
    }

    View2d FitScene2d(const SceneDocument& scene)
    {
        ValidateScene(scene);
        if (scene.game != "2d") {
            throw std::invalid_argument("2D view requires a 2D scene");
        }
        auto minimum = Point2d{scene.settings.bounds_min[0], scene.settings.bounds_min[1]};
        auto maximum = Point2d{scene.settings.bounds_max[0], scene.settings.bounds_max[1]};
        for (const auto& object : scene.objects) {
            if (object.kind == "game_camera") {
                for (auto axis = 0u; axis < 2; ++axis) {
                    minimum[axis] = std::min(minimum[axis], -double(object.view_size[axis]) / 2);
                    maximum[axis] = std::max(maximum[axis], double(object.view_size[axis]) / 2);
                }
            }
        }
        return {{(minimum[0] + maximum[0]) / 2, (minimum[1] + maximum[1]) / 2},
                std::max(maximum[1] - minimum[1], (maximum[0] - minimum[0]) * 9 / 16) * 1.1};
    }

    View2d ZoomView2d(const Mapping2d& mapping, Point2d anchor, double factor)
    {
        if (std::isfinite(factor) == false || factor <= 0) {
            throw std::invalid_argument("zoom factor must be positive and finite");
        }
        // カーソル下のワールド位置を固定して表示の中心を移す。
        const auto world = mapping.ToWorld(anchor);
        auto view = mapping.view;
        view.height *= factor;
        for (auto axis = 0u; axis < 2; ++axis) {
            view.center[axis] = world[axis] + (view.center[axis] - world[axis]) * factor;
        }
        ValidateView2d(view);
        return view;
    }
} // namespace gengine
