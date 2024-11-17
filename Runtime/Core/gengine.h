#pragma once

#include "gn_dll.h"

namespace gngin
{

class GENGINE_RUNTIME_API Engine
{
public:
    static bool InitializeApplication(int argc, char** argv, bool in_editor = false);

    static void RegisterGameInstance(class GameInstance* instance);

    static int LaunchApplication();

    static class Application* GetApplicationRef();
};

}  // namespace gngin
