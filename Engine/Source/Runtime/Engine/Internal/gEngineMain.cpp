#include <cstdint>

#include <Engine.h>
#include <SingletonBase.h>

namespace gngin
{
    int32_t gEngineMain(int argc, char** argv)
    {
        if (!gngin::initializeEngine())
        {
            return -1;
        }

        SingletonFinalizer::finalizeAll();
        return 0;
    }
}
