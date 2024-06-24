#include <gngin_type.h>

#include <Engine.h>
#include <Engine_Internal.h>
#include <SingletonBase.h>

namespace gngin
{
    GN_BOOL engine_initialized = GN_FALSE;

    GN_BOOL initializeEngine()
    {
        if (engine_initialized)
        {
            return GN_FALSE;
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
        engine_initialized = GN_FALSE;
    }

    bool isEngineInitialized()
    {
        return engine_initialized;
    }
}
