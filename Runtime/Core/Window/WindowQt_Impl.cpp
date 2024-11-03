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

bool WindowQt_Impl::Open()
{
    window_->show();

    return true;
}
