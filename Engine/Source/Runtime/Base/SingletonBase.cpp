#include "SingletonBase.h"

#include <gngin_type.h>

namespace
{
    constexpr GnginUint32 max_finalizers = 64;
    
    std::mutex finalizer_mutex;
    GnginUint32 finalizer_num = 0;
    ::gngin::SingletonFinalizer::FinalizeFunction finalizers[max_finalizers];
}

namespace gngin
{
    void SingletonFinalizer::addFinalizer(FinalizeFunction finalize_function)
    {
        std::lock_guard<std::mutex> lock(finalizer_mutex);
        if (finalizer_num < max_finalizers)
        {
            finalizers[finalizer_num++] = finalize_function;
        }
    }

    void SingletonFinalizer::finalizeAll()
    {
        std::lock_guard<std::mutex> lock(finalizer_mutex);
        for (GnginUint32 i = finalizer_num - 1; i >= 0; --i)
        {
            (*finalizers[i])();
        }

        finalizer_num = 0;
    }
}
