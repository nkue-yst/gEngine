#pragma once

#include "gn_dll.h"

#include <memory>

namespace gngin
{

class GENGINE_RUNTIME_API Renderer
{
public:
    Renderer();
    ~Renderer();

    void Initialize();
    void Draw();

private:
    std::unique_ptr<class Renderer_Impl> impl_;
};

}  // namespace gngin
