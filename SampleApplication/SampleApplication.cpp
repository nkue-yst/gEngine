#include "Core/Application/Application.h"
#include "Core/Logging/Logging.h"

#include "GameInstance/GameInstance.h"

using namespace gngin;

int main(int argc, char** argv)
{
    Application* app = new Application(argc, argv);

    /* ゲームインスタンスの設定 */
    GameInstance* g_instance = new GameInstance();
    app->SetGameInstance(g_instance);

    g_instance->SetTitle("gEngine - SampleApplication");

    Logging::message("Start \"SampleApplication\"...", true);
    return app->Exec();
}
