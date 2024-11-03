#include "Application.h"

#include "Application_Impl.h"
#include "ApplicationQt_Impl.h"

using namespace gngin;

Application::Application(int argc, char** argv)
{
    impl_ = std::make_unique<ApplicationQt_Impl>(argc, argv);
}

Application::~Application()
{
}

void Application::SetGameInstance(GameInstance* game_instance)
{
    impl_->SetGameInstance(game_instance);
}

bool Application::Exec()
{
    return impl_->Exec();
}
