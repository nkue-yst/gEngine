#pragma once
#include "scene_document.hpp"

namespace gengine
{
    // 右手系のゲーム座標を固定斜めカメラから見る。行列の格納規約はrEngineと共通。
    void GameCamera(float* view_projection, float aspect);
    void GameCamera(float* view_projection, float aspect, const SceneObject& camera);
} // namespace gengine
