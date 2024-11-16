#include "Viewport.h"
#include "ViewportRenderer.h"

Viewport::Viewport(QWidget *parent)
{
    renderer_ = std::make_unique<ViewportRenderer>(this);

    /* レイアウト設定 */
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
