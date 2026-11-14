#pragma once
#include "scene_document.hpp"
#include <array>

namespace gengine
{
    using Point2d = std::array<double, 2>;

    // 表示用の中心と高さ。文書のゲーム用カメラとは独立して保持する。
    struct View2d
    {
        Point2d center{};
        double height = 9;
        bool operator==(const View2d&) const = default;
    };

    // 描画と入力が同じ16:9の領域を使い、余白をゲーム座標へ混ぜない。
    struct Mapping2d
    {
        View2d view;
        double left = 0;
        double top = 0;
        double width = 0;
        double height = 0;
        bool Contains(Point2d pixel) const;
        Point2d ToWorld(Point2d pixel) const;
        Point2d ToPixel(Point2d world) const;
    };

    Mapping2d MakeMapping2d(View2d view, double width, double height);
    View2d FitScene2d(const SceneDocument& scene);
    View2d ZoomView2d(const Mapping2d& mapping, Point2d anchor, double factor);
    void ValidateView2d(const View2d& view);
    enum class Handle2d
    {
        kNone,
        kX,
        kY,
        kPlane
    };
    Point2d HandleDirection2d(const Mapping2d& mapping, Point2d center);
    Handle2d HitHandle2d(Point2d center, Point2d pixel, Point2d direction = {1, 1});
    std::string PickScene2d(const SceneDocument& scene, const Mapping2d& mapping, Point2d pixel);
} // namespace gengine
