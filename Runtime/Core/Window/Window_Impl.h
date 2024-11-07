#pragma once

#include "Window.h"

#include "../Renderer/Renderer.h"

#include <string>

namespace gngin
{

class GENGINE_RUNTIME_API Window_Impl
{
public:
    Window_Impl();
    ~Window_Impl() = default;

    virtual void SetWindowTitle(std::string title) = 0;
    virtual bool Open() = 0;
    virtual void Update() = 0;

    virtual bool IsRunning() = 0;

protected:
    std::unique_ptr<class Renderer> renderer_;
};

}
