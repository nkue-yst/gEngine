#pragma once

#include <cstdint>

#ifndef GENGINE_RUNTIME_API
#define GENGINE_RUNTIME_API
#endif

namespace gngin
{

class GENGINE_RUNTIME_API Main
{
public:
    Main();
    ~Main();

    int32_t Exec(int argc, char** argv);
};

}
