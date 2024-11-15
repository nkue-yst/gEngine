#include "Application.h"

#include "Application_Impl.h"

#include "../Logging/Logging.h"

using namespace gngin;

Application::Application(int argc, char** argv)
{
    impl_ = std::make_unique<Application_Impl>(argc, argv);

    Logging::message("GnCore: Instance of \"Application\" is created.", true);
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
