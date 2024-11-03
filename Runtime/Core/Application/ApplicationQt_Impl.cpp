#include "ApplicationQt_Impl.h"

#include "GameInstance.h"

#include "../CoreManager/CoreManager.h"
#include "../Window/WindowQt_Impl.h"

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

    return app_->exec();
}
