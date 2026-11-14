#pragma once
#include "scene2d.hpp"
#include "scene_actions.hpp"
#include "view3d.hpp"

namespace gengine::editor
{
    struct ViewportState
    {
        std::string document_id;
        std::uint64_t revision = 0;
        std::vector<double> center;
        double height = 9;
        std::string selected_id;
        bool transient = false;
        std::optional<double> yaw_degrees;
        std::optional<double> pitch_degrees;
    };
    template <> struct Record<ViewportState>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &ViewportState::document_id),
                              MakeField("revision", &ViewportState::revision),
                              MakeField("center", &ViewportState::center),
                              MakeField("height", &ViewportState::height),
                              MakeField("selected_id", &ViewportState::selected_id),
                              MakeField("transient", &ViewportState::transient),
                              MakeField("yaw_degrees", &ViewportState::yaw_degrees),
                              MakeField("pitch_degrees", &ViewportState::pitch_degrees)};
        }
    };
    struct ViewportSet
    {
        std::string document_id;
        std::uint64_t expected_revision = 0;
        std::vector<double> center;
        double height = 9;
        std::optional<double> yaw_degrees;
        std::optional<double> pitch_degrees;
    };
    template <> struct Record<ViewportSet>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("document_id", &ViewportSet::document_id),
                              MakeField("expected_revision", &ViewportSet::expected_revision),
                              MakeField("center", &ViewportSet::center),
                              MakeField("height", &ViewportSet::height),
                              MakeField("yaw_degrees", &ViewportSet::yaw_degrees),
                              MakeField("pitch_degrees", &ViewportSet::pitch_degrees)};
        }
    };
    // 入力途中の状態はEditorだけが所有し、確定文書へ書き戻さない。
    struct ViewportGesture
    {
        Mapping2d mapping;
        Point2d start;
        Point2d original;
        Point2d candidate;
        Handle2d handle = Handle2d::kNone;
        bool pan = false;
        std::uint64_t revision = 0;
    };

    enum class Gesture3dKind
    {
        kAxis,
        kPan,
        kOrbit
    };
    struct ViewportGesture3d
    {
        Mapping3d mapping;
        Point2d start;
        Point3d original{};
        Point3d candidate{};
        Gesture3dKind kind = Gesture3dKind::kAxis;
        unsigned axis = 0;
        std::string object_id;
        std::uint64_t revision = 0;
    };
} // namespace gengine::editor
