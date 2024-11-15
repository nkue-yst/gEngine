#pragma once

#include "Application.h"

#include <QApplication>

namespace gngin
{

class GENGINE_RUNTIME_API Application_Impl
{
public:
    Application_Impl(int argc, char** argv);
    ~Application_Impl();

    void SetGameInstance(class GameInstance* game_instance);

    bool Exec();

private:
    void MainLoop();

    class CoreManager* core_;
    class GameInstance* game_instance_;

    QApplication* app_;
};

}  // namespace gngin
