#pragma once

#include "Application.h"

namespace gngin
{

class GENGINE_RUNTIME_API Application_Impl
{
public:
    Application_Impl() :core_(nullptr), game_instance_(nullptr) {}
    virtual ~Application_Impl() = default;

    void SetGameInstance(class GameInstance* game_instance);

    virtual bool Exec() = 0;

protected:
    class CoreManager* core_;
    class GameInstance* game_instance_;
};

}
