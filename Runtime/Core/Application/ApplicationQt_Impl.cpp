#include "ApplicationQt_Impl.h"

#include "GameInstance.h"

#include "../CoreManager/CoreManager.h"
#include "../Window/WindowQtGL_Impl.h"

#include <QThread>
#include <QTimer>
#include <QElapsedTimer>

using namespace gngin;

ApplicationQt_Impl::ApplicationQt_Impl(int argc, char** argv)
{
    app_  = new QApplication(argc, argv);
    core_ = new CoreManager();
}

ApplicationQt_Impl::~ApplicationQt_Impl()
{
}

bool ApplicationQt_Impl::Exec()
{
    core_->GetWindow()->SetWindowTitle(game_instance_->GetTitle());
    core_->GetWindow()->Open();

    this->MainLoop();

    return true;
}

void ApplicationQt_Impl::MainLoop()
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
