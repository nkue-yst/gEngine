#pragma once

#include "Application.h"

namespace gngin
{

class GENGINE_RUNTIME_API Application_Impl
{
public:
    virtual ~Application_Impl() = default;

    virtual bool Exec() = 0;

protected:
    class CoreManager* core_;
};

}
