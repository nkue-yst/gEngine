#include "assets.hpp"
#include "scene3d.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <rengine/renderer.hpp>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (condition == false) {
            throw std::runtime_error(message);
        }
    }
    template <class T> T Take(rengine::Expected<T> result)
    {
        if (result.has_value() == false) {
            throw std::runtime_error(result.error().message);
        }
        return std::move(*result);
    }
} // namespace

int wmain(int, wchar_t** argv)
{
    try {
        auto settings = rengine::Settings{};
        settings.backend = "d3d12";
        settings.backend_directory = std::filesystem::absolute(argv[0]).parent_path() / "backends";
        auto renderer = Take(rengine::Renderer::Create(settings));
        auto metrics = gengine::AssetMetrics{};
        const auto source = gengine::AssetSource(settings.backend_directory.parent_path() / "content");
        const auto data = gengine::DecodeMesh(source.Read("assets/sphere.gmesh", 4 * 1024 * 1024, metrics));
        const auto mesh = Take(
            renderer->CreateMesh({data.vertices.data(), static_cast<std::uint32_t>(data.vertices.size()),
                                  data.indices.data(), static_cast<std::uint32_t>(data.indices.size()), nullptr, 0}));
        const auto target = Take(renderer->CreateTarget({64, 64}));
        auto pass = rengine::DefaultPass();
        pass.target = target;
        gengine::GameCamera(pass.view_projection, 1);

        // 注視点、軸の向き、カメラ背後を調べ、行列の転置や左右反転を検出する。
        const auto project = [&](float x, float y, float z) {
            auto value = std::array<float, 4>{};
            for (auto column = 0u; column < 4; ++column) {
                const auto* m = pass.view_projection;
                value[column] = x * m[column] + y * m[4 + column] + z * m[8 + column] + m[12 + column];
            }
            return value;
        };
        const auto center = project(0, 2, 0);
        Require(std::abs(center[0]) < 0.00001f && std::abs(center[1]) < 0.00001f && center[2] > 0 &&
                    center[2] < center[3],
                "camera target projection");
        Require(project(1, 2, 0)[0] > 0 && project(0, 3, 0)[1] > 0 && project(0, 2, 1)[0] < 0,
                "right handed world axes");
        Require(project(24, 18, 28)[3] < 0, "camera back must have negative W");

        auto material = rengine::DefaultMaterial();
        material.depth_test = 1;
        material.depth_write = 1;
        const auto handle = Take(renderer->CreateMaterial(material));
        auto instances = std::array{rengine::DefaultInstance(), rengine::DefaultInstance()};
        // 手前の赤と奥の青を同じ視線上へ置き、提出順に依存しない遮蔽を確かめる。
        instances[0].world[12] = 6;
        instances[0].world[13] = 6;
        instances[0].world[14] = 7;
        instances[0].color[1] = 0;
        instances[0].color[2] = 0;
        instances[1].world[13] = 2;
        instances[1].color[0] = 0;
        instances[1].color[1] = 0;
        auto draw = rengine::Draw{};
        draw.mesh = mesh;
        draw.material = handle;
        draw.instances = instances.data();
        draw.instance_count = 2;
        pass.draws = &draw;
        pass.draw_count = 1;
        auto pixels = std::array<std::uint8_t, 64 * 64 * 4>{};
        const auto render = [&] {
            Require(renderer->Render({&pass, 1, 0}).has_value(), "sphere render failed");
            Require(renderer->ReadTarget(target, pixels).has_value(), "sphere readback failed");
        };
        constexpr auto kCenter = (32 * 64 + 32) * 4;
        for (auto order = 0; order < 2; ++order) {
            render();
            Require(pixels[kCenter] == 255 && pixels[kCenter + 2] == 0, "near sphere must occlude far sphere");
            std::swap(instances[0], instances[1]);
        }
        // 深度なしでは後から提出した青が覆うことも確認し、空の画像による偽合格を防ぐ。
        material.depth_test = 0;
        material.depth_write = 0;
        Require(renderer->UpdateMaterial(handle, material).has_value(), "material update failed");
        render();
        Require(pixels[kCenter] == 0 && pixels[kCenter + 2] == 255, "depth control image");
        std::cout << "right handed camera, sphere mesh and order independent GPU depth passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
