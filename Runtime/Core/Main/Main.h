#pragma once

#include "gn_dll.h"

#include <cstdint>

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
