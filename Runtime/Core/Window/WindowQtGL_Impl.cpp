#include "WindowQtGL_Impl.h"

#include "../Renderer/RendererQtGL_Impl.h"

using namespace gngin;

WindowQtGL_Impl::WindowQtGL_Impl(QWidget* parent, Qt::WindowFlags f)
    : QOpenGLWidget(parent, f)
    , Window_Impl()
{

}

WindowQtGL_Impl::~WindowQtGL_Impl()
{

}

void WindowQtGL_Impl::initializeGL()
{
    renderer_->Initialize();
}

void WindowQtGL_Impl::resizeGL(int w, int h)
{

}

void WindowQtGL_Impl::paintGL()
{
    renderer_->Draw();
}

void WindowQtGL_Impl::SetWindowTitle(std::string title)
{
    this->setWindowTitle(QString::fromStdString(title));
}

void WindowQtGL_Impl::Update()
{
    this->update();
}

bool WindowQtGL_Impl::Open()
{
    this->show();

    return true;
}

bool WindowQtGL_Impl::IsRunning()
{
    return this->isVisible();
}
