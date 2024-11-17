#include "gengine.h"

#include "Classes/GameInstance/GameInstance.h"

#include "Application/Application.h"

using namespace gngin;

namespace
{
    static class Application* app = nullptr;
}

bool Engine::InitializeApplication(int argc, char** argv, bool in_editor)
{
    if (app)
    {
        delete app;
        app = nullptr;
    }

    app = new Application(argc, argv, in_editor);

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

Application* Engine::GetApplicationRef()
{
    return app;
}
