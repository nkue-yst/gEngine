#pragma once

#include "Application_Impl.h"

#include <QApplication>

using namespace gngin;

class GENGINE_RUNTIME_API ApplicationQt_Impl : public Application_Impl
{
public:
    ApplicationQt_Impl(int argc, char** argv);
    ~ApplicationQt_Impl();

    bool Exec() override;

private:
    QApplication* app_;
};
