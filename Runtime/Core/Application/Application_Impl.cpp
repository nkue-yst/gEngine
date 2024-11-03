#include "Application_Impl.h"

#include "GameInstance.h"

using namespace gngin;

void Application_Impl::SetGameInstance(GameInstance* game_instance)
{
    game_instance_ = game_instance;
}
