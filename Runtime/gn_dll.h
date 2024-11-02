#pragma once

#ifdef GENGINE_RUNTIME_DLL
    #define GENGINE_RUNTIME_API __declspec(dllexport)
#else
    #define GENGINE_RUNTIME_API __declspec(dllimport)
#endif
