#pragma once

#include "gn_dll.h"

#include <memory>

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
    std::unique_ptr<class Application_Impl> impl_;
};

}  // namespace gngin
