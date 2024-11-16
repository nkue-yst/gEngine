#pragma once

#include "gn_dll.h"

#include <memory>

#include <QApplication>

namespace gngin
{

/* アプリのメインループ処理等を管理するクラス */
class GENGINE_RUNTIME_API Application
{
public:
    Application(int argc, char** argv);
    ~Application();

    void SetGameInstance(class GameInstance* game_instance);

    bool Exec();

private:
    void MainLoop();

    class CoreManager* core_;
    class GameInstance* game_instance_;

    QApplication* app_;
};

}  // namespace gngin
