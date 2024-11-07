#pragma once

#include "gn_dll.h"

namespace gngin
{

class GENGINE_RUNTIME_API Renderer_Impl
{
public:
    virtual ~Renderer_Impl() = default;

    virtual void Initialize() = 0;
    virtual void Draw() = 0;
};

}
