#include "Application_Impl.h"

#include "GameInstance.h"

#include "../CoreManager/CoreManager.h"
#include "../Window/Window.h"

#include <QThread>
#include <QTimer>
#include <QElapsedTimer>

using namespace gngin;

Application_Impl::Application_Impl(int argc, char** argv)
{
    app_  = new QApplication(argc, argv);
    core_ = new CoreManager();
}

Application_Impl::~Application_Impl()
{
}

void Application_Impl::SetGameInstance(GameInstance* game_instance)
{
    game_instance_ = game_instance;
}

bool Application_Impl::Exec()
{
    core_->GetWindow()->SetWindowTitle(game_instance_->GetTitle());
    core_->GetWindow()->Open();

    this->MainLoop();

    return true;
}

void Application_Impl::MainLoop()
{
    QElapsedTimer frame_timer;
    frame_timer.start();

    while (core_->GetWindow()->IsRunning())
    {
        app_->processEvents();

        if (frame_timer.elapsed() < 16)
        {
            continue;
        }
        frame_timer.restart();

        // TODO: 描画更新
        core_->GetWindow()->Update();

        QThread::usleep(500);
    }
}
