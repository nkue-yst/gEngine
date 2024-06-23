#include <gngin_type.h>

#include <Engine.h>
#include <Engine_Internal.h>
#include <SingletonBase.h>

namespace gngin
{
    GNGIN_BOOL engine_initialized = GNGIN_FALSE;

    GNGIN_BOOL initializeEngine()
    {
        if (engine_initialized)
        {
            return GNGIN_FALSE;
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
        engine_initialized = GNGIN_FALSE;
    }

    bool isEngineInitialized()
    {
        return engine_initialized;
    }
}
