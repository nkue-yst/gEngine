#include <gngin_type.h>

#include <Engine.h>
#include <SingletonBase.h>

namespace gngin
{
    GnginSint32 gEngineMain(GnginSint32 argc, GnginChar8** argv)
    {
        if (!gngin::initializeEngine())
        {
            return -1;
        }

        SingletonFinalizer::finalizeAll();
        return 0;
    }
}
