#include <SingletonBase.h>

namespace
{
    constexpr uint32_t max_finalizers = 64;
    
    std::mutex finalizer_mutex;
    uint32_t finalizer_num = 0;
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
        for (uint32_t i = finalizer_num - 1; i >= 0; --i)
        {
            (*finalizers[i])();
        }

        finalizer_num = 0;
    }
}
