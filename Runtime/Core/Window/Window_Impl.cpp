#include "Window_Impl.h"

#include "../Renderer/Renderer.h"

using namespace gngin;

Window_Impl::Window_Impl(QWidget* parent, Qt::WindowFlags f)
    : QOpenGLWidget(parent, f)
{
    renderer_ = std::make_unique<Renderer>();
}

Window_Impl::~Window_Impl()
{
}

void Window_Impl::SetWindowTitle(std::string title)
{
    this->setWindowTitle(QString::fromStdString(title));
}

bool Window_Impl::Open()
{
    this->show();

    return true;
}

void Window_Impl::Update()
{
    renderer_->Draw();
}

bool Window_Impl::IsRunning()
{
    return this->isVisible();
}

void Window_Impl::initializeGL()
{
    renderer_->Initialize();
}
