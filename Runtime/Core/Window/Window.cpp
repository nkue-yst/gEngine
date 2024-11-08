﻿#include "Window.h"

#include "Window_Impl.h"
#include "WindowQtGL_Impl.h"

#include "../Logging/Logging.h"

using namespace gngin;

/* ウィンドウクラス(実装は Window_Impl クラスへ移譲) */
Window::Window()
{
    impl_ = std::make_unique<WindowQtGL_Impl>();

    Logging::message("GnCore: Instance of \"Window\" is created.", true);
}

Window::~Window()
{
}

void Window::SetWindowTitle(std::string title)
{
    impl_->SetWindowTitle(title);
}

void Window::Update()
{
    impl_->Update();
}

bool Window::Open()
{
    return impl_->Open();
}

bool Window::IsRunning()
{
    return impl_->IsRunning();
}
