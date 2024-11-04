#pragma once

#include "gn_dll.h"

#include <string>

namespace gngin
{

class GENGINE_RUNTIME_API Logging
{
public:
    static void message(std::string text, bool debug_only = false);
    static void warning(std::string text, bool debug_only = false);
    static void error(  std::string text, bool debug_only = false);
};

}  // namespace gngin
