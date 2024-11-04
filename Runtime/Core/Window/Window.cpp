#include "Window.h"

#include "Window_Impl.h"
#include "WindowQt_Impl.h"

#include "../Logging/Logging.h"

using namespace gngin;

/* ウィンドウクラス(実装は Window_Impl クラスへ移譲) */
Window::Window()
{
    impl_ = std::make_unique<WindowQt_Impl>();

    Logging::message("GnCore: Instance of \"Window\" is created.", true);
}

Window::~Window()
{
}

void Window::SetWindowTitle(std::string title)
{
    impl_->SetWindowTitle(title);
}

bool Window::Open()
{
    return impl_->Open();
}
