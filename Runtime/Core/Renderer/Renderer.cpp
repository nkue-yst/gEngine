#include "Renderer.h"

#include "Renderer_Impl.h"
#include "RendererQtGL_Impl.h"

#include "../Logging/Logging.h"

using namespace gngin;

Renderer::Renderer()
{
    impl_ = std::make_unique<RendererQtGL_Impl>();

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
