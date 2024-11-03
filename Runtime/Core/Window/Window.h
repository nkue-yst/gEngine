#pragma once

#include "gn_dll.h"

#include <memory>
#include <string>

namespace gngin
{

/* ゲームアプリ用ウィンドウクラス */
class GENGINE_RUNTIME_API Window
{
public:
    Window();
    ~Window();

    void SetWindowTitle(std::string title);
    bool Open();

private:
    std::unique_ptr<class Window_Impl> impl_;
};

}
