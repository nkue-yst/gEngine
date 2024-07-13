#include <MinimumProject.h>

#include <Engine.h>

int main()
{
    GN_BOOL initialized = gngin::initializeEngine();

    if (initialized)
    {
        gngin::finalizeEngine();
    }

    return 0;
}
