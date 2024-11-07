#include "RendererQtGL_Impl.h"

using namespace gngin;

RendererQtGL_Impl::RendererQtGL_Impl(QObject* parent)
    : QObject(parent)
{

}

RendererQtGL_Impl::~RendererQtGL_Impl()
{

}

void RendererQtGL_Impl::Initialize()
{
    initializeOpenGLFunctions();
    glClearColor(0.5f, 0.8f, 0.5f, 1.0f);
}

void RendererQtGL_Impl::Draw()
{
    glClear(GL_COLOR_BUFFER_BIT);
}
