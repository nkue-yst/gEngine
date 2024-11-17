#include "gengine.h"

#include "Application/Application.h"

using namespace gngin;

namespace
{
    static class Application* app = nullptr;
}

bool Engine::InitializeApplication(int argc, char** argv)
{
    if (app)
    {
        delete app;
        app = nullptr;
    }

    app = new Application(argc, argv);

    return true;
}

void Engine::RegisterGameInstance(GameInstance* instance)
{
    app->SetGameInstance(instance);
}

int Engine::LaunchApplication()
{
    return (app->Exec() ? 0 : 1);
}
