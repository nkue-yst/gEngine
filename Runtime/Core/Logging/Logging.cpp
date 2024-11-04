#include "Logging.h"

#include <iostream>

using namespace gngin;

namespace
{
    const char* HEADER = "[GENGINE_LOG]: ";
}

void Logging::message(std::string text, bool debug_only)
{
    if (debug_only)
    {
#ifndef GENGINE_DEBUG
        return;
#endif
    }
    std::cout << HEADER << text << std::endl;
}

void Logging::warning(std::string text, bool debug_only)
{
    if (debug_only)
    {
#ifdef GENGINE_DEBUG
        return;
#endif
    }
    std::cout << HEADER << "\033[33m" << text << "\033[0m" << std::endl;
}

void Logging::error(std::string text, bool debug_only)
{
    if (debug_only)
    {
#ifdef GENGINE_DEBUG
        return;
#endif
    }
    std::cout << HEADER << "\033[31m" << text << "\033[0m" << std::endl;
}
