#include "ViewportRenderer.h"

ViewportRenderer::ViewportRenderer(QObject* parent)
    : QObject(parent)
{
}

ViewportRenderer::~ViewportRenderer()
{
}

void ViewportRenderer::Initialize()
{
    initializeOpenGLFunctions();
    glClearColor(0.8f, 0.5f, 0.5f, 1.0f);
}

void ViewportRenderer::Draw()
{
    glClear(GL_COLOR_BUFFER_BIT);

    // ここでエディタ上のゲームオブジェクトの描画を行う
    //foreach(ObjectsInTheScene)...
}
