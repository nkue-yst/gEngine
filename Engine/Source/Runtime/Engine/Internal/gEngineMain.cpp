#include <gngin_type.h>

#include <Engine.h>
#include <SingletonBase.h>

namespace gngin
{
    GnSint32 gEngineMain(GnSint32 argc, GnChar8** argv)
    {
        if (!gngin::initializeEngine())
        {
            return -1;
        }

        SingletonFinalizer::finalizeAll();
        return 0;
    }
}
