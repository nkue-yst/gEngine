#include "CoreManager.h"

#include "../Window/Window.h"

using namespace gngin;

CoreManager::CoreManager()
{
    window_ = new Window();
}

CoreManager::~CoreManager()
{
}
