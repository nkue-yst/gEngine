#include "scene2d.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* reason)
    {
        if (condition == false) {
            throw std::runtime_error(reason);
        }
    }
    void Near(gengine::Point2d actual, gengine::Point2d expected)
    {
        Require(std::abs(actual[0] - expected[0]) < 1e-9 && std::abs(actual[1] - expected[1]) < 1e-9,
                "coordinate mismatch");
    }
} // namespace

int main()
{
    try {
        // 横長・縦長・正方形で余白と逆変換を確かめる。
        for (const auto size : {gengine::Point2d{640, 360}, gengine::Point2d{1200, 300}, gengine::Point2d{400, 900}}) {
            const auto mapping = gengine::MakeMapping2d({{2, -3}, 18}, size[0], size[1]);
            Near(mapping.ToWorld(mapping.ToPixel({-4, 5})), {-4, 5});
            Near(mapping.ToPixel({2, -3}), {size[0] / 2, size[1] / 2});
            Require(mapping.ToPixel({2, -2})[1] < size[1] / 2, "Y direction");
            Require(mapping.Contains({mapping.left - 1, mapping.top}) == false, "letterbox accepted");
            const auto anchor = gengine::Point2d{size[0] * .6, size[1] * .4};
            const auto zoomed = gengine::MakeMapping2d(gengine::ZoomView2d(mapping, anchor, .5), size[0], size[1]);
            Near(mapping.ToWorld(anchor), zoomed.ToWorld(anchor));
        }
        const auto scene = gengine::DefaultScene(false);
        const auto fit = gengine::FitScene2d(scene);
        const auto small = gengine::MakeMapping2d(fit, 380, 350);
        const auto top = small.ToPixel({0, 3});
        const auto direction = gengine::HandleDirection2d(small, top);
        Require(direction[1] == -1, "top handle should face inward");
        Require(gengine::HitHandle2d(top, {top[0], top[1] + 44}, direction) == gengine::Handle2d::kY,
                "inward handle hit differs from draw");
        Require(fit.height > 9, "fit needs margin");
        Near(fit.center, {0, 0});
        auto rejected = false;
        try {
            gengine::MakeMapping2d({}, 0, 360);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        Require(rejected, "zero viewport accepted");
        // ゲーム範囲より大きいカメラも全体表示に含める。
        auto wide_scene = scene;
        wide_scene.objects[2].view_size = {64, 36};
        Require(gengine::FitScene2d(wide_scene).height > 36, "camera frame omitted from fit");
        for (const auto invalid :
             {gengine::View2d{{0, 0}, 0}, gengine::View2d{{std::numeric_limits<double>::infinity(), 0}, 9},
              gengine::View2d{{0, 0}, std::numeric_limits<double>::quiet_NaN()}}) {
            rejected = false;
            try {
                gengine::ValidateView2d(invalid);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            Require(rejected, "invalid view accepted");
        }
        std::cout << "2D mapping, fit and anchored zoom passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
