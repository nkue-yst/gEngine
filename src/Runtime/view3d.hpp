#pragma once
#include "scene2d.hpp"
#include <array>

namespace gengine
{
    using Point3d = std::array<double, 3>;

    // 編集専用の正射影カメラ。ゲーム用カメラや保存シーンには書き込まない。
    struct View3d
    {
        Point3d center{};
        double height = 24;
        double yaw_degrees = 45;
        double pitch_degrees = 30;
        bool operator==(const View3d&) const = default;
    };
    struct Ray3d
    {
        Point3d origin;
        Point3d direction;
    };
    struct Line3d
    {
        Point3d first;
        Point3d second;
    };
    struct Mapping3d
    {
        View3d view;
        Mapping2d area;
        Point3d right;
        Point3d up;
        Point3d back;
        Point2d ToPixel(Point3d world) const;
        Point3d OnViewPlane(Point2d pixel) const;
        Ray3d Ray(Point2d pixel) const;
        double Distance() const;
        bool Visible(Point3d world) const;
    };
    struct Axis3d
    {
        Point2d direction{};
        Point2d projected_unit{};
        bool visible = false;
    };

    Point3d Add3d(Point3d a, Point3d b);
    Point3d Subtract3d(Point3d a, Point3d b);
    Point3d Multiply3d(Point3d point, double factor);
    double Dot3d(Point3d a, Point3d b);
    Point3d Cross3d(Point3d a, Point3d b);
    Point3d Unit3d(Point3d point);
    void ValidateView3d(const View3d& view);
    Mapping3d MakeMapping3d(View3d view, double width, double height);
    View3d FitScene3d(const SceneDocument& scene);
    View3d ZoomView3d(const Mapping3d& mapping, Point2d anchor, double factor);
    void EditorCamera3d(float* view_projection, const Mapping3d& mapping);
    // 描画とヒット判定が同じ画面上の軸を使う。視線に重なる軸は操作しない。
    std::array<Axis3d, 3> Axes3d(const Mapping3d& mapping, Point3d position);
    // カメラの位置・向き・画角を示す短い錐台と注視線。無限の表示範囲を表すものではない。
    std::array<Line3d, 9> CameraLines3d(const SceneObject& camera);
} // namespace gengine
