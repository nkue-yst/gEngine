#include "Window.h"

#include "../Logging/Logging.h"
#include "../Renderer/Renderer.h"

using namespace gngin;

/* ウィンドウクラス(実装はWindow_Implクラスへ移譲) */
Window::Window(QWidget* parent, Qt::WindowFlags f)
    : QOpenGLWidget(parent, f)
{
    Logging::message("GnCore: Instance of \"Window\" is created.", true);

    renderer_ = std::make_unique<Renderer>();
}

Window::~Window()
{
}

void Window::SetWindowTitle(std::string title)
{
    this->setWindowTitle(QString::fromStdString(title));
}

void Window::Update()
{
    this->update();
}

bool Window::Open()
{
    this->show();

    return true;
}

bool Window::IsRunning()
{
    return this->isVisible();
}

void Window::initializeGL()
{
    renderer_->Initialize();
}

void Window::paintGL()
{
    renderer_->Draw();
}
