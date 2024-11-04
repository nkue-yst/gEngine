#include "CoreManager.h"

#include "../Logging/Logging.h"
#include "../Window/Window.h"

using namespace gngin;

CoreManager::CoreManager()
{
    window_ = new Window();

    Logging::message("GnCore: Instance of \"CoreManager\" is created.", true);
}

CoreManager::~CoreManager()
{
}
