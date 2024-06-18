#include <Engine.h>
#include <Engine_Internal.h>
#include <SingletonBase.cpp>

namespace gngin
{
    bool engine_initialized = false;

    bool initializeEngine()
    {
        if (engine_initialized)
        {
            return false;
        }

        engine_initialized = GnginEngine::getInstance()->initialize();

        return engine_initialized;
    }

    void finalizeEngine()
    {
        if (!engine_initialized)
        {
            return;
        }

        GnginEngine::getInstance()->finalize();
        engine_initialized = false;
    }

    bool isEngineInitialized()
    {
        return engine_initialized;
    }
}
