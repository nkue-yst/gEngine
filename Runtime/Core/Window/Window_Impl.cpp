#include "Window_Impl.h"

using namespace gngin;

Window_Impl::Window_Impl()
{
    renderer_ = std::make_unique<Renderer>();
}
