#pragma once

#include "Window.h"

#include <string>

namespace gngin
{

class GENGINE_RUNTIME_API Window_Impl
{
public:
    virtual ~Window_Impl() = default;

    virtual void SetWindowTitle(std::string title) = 0;
    virtual bool Open() = 0;

    virtual bool IsRunning() = 0;
};

}
