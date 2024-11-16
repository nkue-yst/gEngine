#include "Renderer.h"

#include "../Logging/Logging.h"

using namespace gngin;

Renderer::Renderer(QObject* parent)
    : QObject(parent)
{
    Logging::message("GnCore: Instance of \"Renderer\" is created.", true);
}

Renderer::~Renderer()
{
}

void Renderer::Initialize()
{
    initializeOpenGLFunctions();
    glClearColor(0.5f, 0.8f, 0.5f, 1.0f);
}

void Renderer::Draw()
{
    glClear(GL_COLOR_BUFFER_BIT);

    // ここでインゲームオブジェクトの描画を行う
    //foreach(ObjectsInGame)...
}
