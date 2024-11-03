#pragma once

#include "gn_dll.h"

#include <memory>

namespace gngin
{

/* ゲームアプリ用ウィンドウクラス */
class GENGINE_RUNTIME_API Window
{
public:
    Window();
    ~Window();

    bool Open();

private:
    std::unique_ptr<class Window_Impl> impl_;
};

}
