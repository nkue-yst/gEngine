#include "WindowQt_Impl.h"

#include <QMainWindow>

using namespace gngin;

WindowQt_Impl::WindowQt_Impl()
{
    window_ = new QMainWindow();
}

WindowQt_Impl::~WindowQt_Impl()
{
}

void WindowQt_Impl::SetWindowTitle(std::string title)
{
    window_->setWindowTitle(QString::fromStdString(title));
}

bool WindowQt_Impl::Open()
{
    window_->show();

    return true;
}

bool WindowQt_Impl::IsRunning()
{
    return window_->isVisible();
}
