#include "Application.h"

#include "Classes/GameInstance/GameInstance.h"

#include "../CoreManager/CoreManager.h"
#include "../Logging/Logging.h"
#include "../Window/Window.h"

#include <QThread>
#include <QTimer>
#include <QElapsedTimer>

using namespace gngin;

Application::Application(int argc, char** argv, bool isPIE)
    : game_instance_(nullptr)
    , is_PIE_(isPIE)
{
    if (!isPIE)
    {
        app_ = new QApplication(argc, argv);
    }
    else
    {
        app_ = qApp;
    }
    core_ = new CoreManager();

    Logging::message("GnCore: Instance of \"Application\" is created.", true);
}

Application::~Application()
{
}

void Application::SetGameInstance(GameInstance* game_instance)
{
    game_instance_ = game_instance;
}

bool Application::Exec()
{
    if (!is_PIE_)
    {
        core_->GetWindow()->SetWindowTitle(game_instance_->GetTitle());
        core_->GetWindow()->Open();
    }

    this->MainLoop();

    return true;
}

Window* Application::GetWindowRef()
{
    return core_->GetWindow();
}

void Application::MainLoop()
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
