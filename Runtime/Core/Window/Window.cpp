#include "Window.h"

#include "Window_Impl.h"
#include "WindowQt_Impl.h"

using namespace gngin;

/* ウィンドウクラス(実装は Window_Impl クラスへ移譲) */
Window::Window()
{
    impl_ = std::make_unique<WindowQt_Impl>();
}

Window::~Window()
{
}

bool Window::Open()
{
    return impl_->Open();
}
