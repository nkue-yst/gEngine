#pragma once

#include "gn_dll.h"

namespace gngin
{

class GENGINE_RUNTIME_API CoreManager
{
public:
    CoreManager();
    ~CoreManager();

    class Window* GetWindow() { return window_; }

private:
    class Window* window_;
};

}
