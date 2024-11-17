#pragma once

#include "gn_dll.h"

#include "GameInstance/GameInstance.h"

namespace gngin
{

class GENGINE_RUNTIME_API Engine
{
public:
    static bool InitializeApplication(int argc, char** argv);

    static void RegisterGameInstance(GameInstance* instance);

    static int LaunchApplication();
};

}  // namespace gngin
