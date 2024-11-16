#include "Viewport.h"

#include "Window/Window_Impl.h"

Viewport::Viewport(QWidget *parent)
{
    renderer_ = std::make_unique<gngin::Renderer>();

    /* ���C�A�E�g�ݒ� */
    this->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
}

Viewport::~Viewport()
{
}

void Viewport::initializeGL()
{
    renderer_->Initialize();
}

void Viewport::paintGL()
{
    renderer_->Draw();
}
