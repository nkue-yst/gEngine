#include "Renderer.h"

#include "Renderer_Impl.h"

#include "../Logging/Logging.h"

using namespace gngin;

Renderer::Renderer()
{
    impl_ = std::make_unique<Renderer_Impl>();

    Logging::message("GnCore: Instance of \"Renderer\" is created.", true);
}

Renderer::~Renderer()
{
}

void Renderer::Initialize()
{
    impl_->Initialize();
}

void Renderer::Draw()
{
    impl_->Draw();
}
