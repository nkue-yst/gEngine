#include "Application.h"

#include "Application_Impl.h"
#include "ApplicationQt_Impl.h"

using namespace gngin;

Application::Application()
{
    impl_ = std::make_unique<ApplicationQt_Impl>();
}

Application::~Application()
{
}

bool Application::Exec()
{
    return impl_->Exec();
}
