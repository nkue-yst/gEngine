#include "Core/Application/Application.h"

#include "GameInstance/GameInstance.h"

using namespace gngin;

int main(int argc, char** argv)
{
    Application* app = new Application(argc, argv);

    /* ゲームインスタンスの設定 */
    GameInstance* g_instance = new GameInstance();
    g_instance->SetTitle("gEngine - SampleApplication");

    app->SetGameInstance(g_instance);
    
    return app->Exec();
}
