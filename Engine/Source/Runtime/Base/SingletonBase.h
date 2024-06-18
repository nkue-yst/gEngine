#pragma once

#include <mutex>

namespace gngin
{
    class SingletonFinalizer
    {
    public:
        using FinalizeFunction = void(*)();

        static void addFinalizer(FinalizeFunction finalize_function);
        static void finalizeAll();
    };

    template<typename T>
    class SingletonBase final
    {
    private:
        static T *instance;
        static std::once is_initialized;

    public:
        static T& getInstance()
        {
            std::call_once(this->is_initialized, create);
            return *this->instance;
        }

    private:
        static void create()
        {
            this->instance = new T();
            SingletonFinalizer::addFinalizer(&SingletonBase<T>::destroy);
        }

        static void destroy()
        {
            delete this->instance;
            this->instance = nullptr;
        }
    };

    template<typename T> std::once_flag SingletonBase<T>::is_initialized;
    template<typename T> T* SingletonBase<T>::instance = nullptr;
}
