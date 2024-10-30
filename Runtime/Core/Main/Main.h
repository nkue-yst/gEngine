#pragma once

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

#ifdef GN_BUILD_WITH_MAIN
int main(int argc, char** argv)
{
    Main gn_main;
    return gn_main.Exec();
}
#endif
