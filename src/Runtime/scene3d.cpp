#include "scene3d.hpp"
#include <DirectXMath.h>
#include <cstring>

namespace gengine
{
    void GameCamera(float* view_projection, float aspect)
    {
        GameCamera(view_projection, aspect, DefaultSceneObject("3d", "game_camera", "camera"));
    }

    void GameCamera(float* view_projection, float aspect, const SceneObject& camera)
    {
        const auto& p = camera.position;
        const auto& t = camera.target;
        const auto view =
            DirectX::XMMatrixLookAtRH(DirectX::XMVectorSet(p[0], p[1], p[2], 1),
                                      DirectX::XMVectorSet(t[0], t[1], t[2], 1), DirectX::XMVectorSet(0, 1, 0, 0));
        const auto projection =
            DirectX::XMMatrixPerspectiveFovRH(camera.fov_degrees * DirectX::XM_PI / 180, aspect, 0.1f, 100);
        auto matrix = DirectX::XMFLOAT4X4{};
        DirectX::XMStoreFloat4x4(&matrix, view * projection);
        std::memcpy(view_projection, &matrix, sizeof(matrix));
    }

} // namespace gengine
