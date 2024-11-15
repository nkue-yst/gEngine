#include "Renderer_Impl.h"

using namespace gngin;

Renderer_Impl::Renderer_Impl(QObject* parent)
    : QObject(parent)
{
}

Renderer_Impl::~Renderer_Impl()
{
}

void Renderer_Impl::Initialize()
{
    initializeOpenGLFunctions();
    glClearColor(0.5f, 0.8f, 0.5f, 1.0f);
}

void Renderer_Impl::Draw()
{
    glClear(GL_COLOR_BUFFER_BIT);
}
