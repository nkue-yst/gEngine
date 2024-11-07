#pragma once

#include "Renderer_Impl.h"

#include <QObject>
#include <QOpenGLFunctions>

namespace gngin
{

class GENGINE_RUNTIME_API RendererQtGL_Impl : public QObject, protected QOpenGLFunctions, public Renderer_Impl
{
Q_OBJECT

public:
    explicit RendererQtGL_Impl(QObject* parent = nullptr);
    virtual ~RendererQtGL_Impl() override;

    void Initialize() override;
    void Draw() override;
};

}
