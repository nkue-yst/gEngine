#pragma once

#include "gn_dll.h"

#include <string>

namespace gngin
{

class GENGINE_RUNTIME_API GameInstance
{
public:
    GameInstance();
    ~GameInstance();

    void SetTitle(std::string title) { title_ = title; }
    
    std::string GetTitle() { return title_; }

private:
    std::string title_;
};

}  // namespace gngin
