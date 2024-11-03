#pragma once

#include "Window.h"

namespace gngin
{

class GENGINE_RUNTIME_API Window_Impl
{
public:
    virtual ~Window_Impl() = default;

    virtual bool Open() = 0;
};

}
