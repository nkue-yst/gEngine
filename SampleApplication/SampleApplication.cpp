#include "gengine.h"

#include "GameInstance/GameInstance.h"

using namespace gngin;

int main(int argc, char** argv)
{
    Engine::InitializeApplication(argc, argv);

    GameInstance game_instance;
    game_instance.SetTitle("SampleApplication");

    Engine::RegisterGameInstance(&game_instance);

    return Engine::LaunchApplication();
}
