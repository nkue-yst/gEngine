#pragma once

#include "Window_Impl.h"

#include <QWidget>

using namespace gngin;

class GENGINE_RUNTIME_API WindowQt_Impl : public Window_Impl
{
public:
    WindowQt_Impl();
    ~WindowQt_Impl();

    bool Open() override;

private:
    QWidget* window_;
};
