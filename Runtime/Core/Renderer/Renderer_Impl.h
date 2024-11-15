#pragma once

#include "gn_dll.h"

#include <QObject>
#include <QOpenGLFunctions>

namespace gngin
{

class GENGINE_RUNTIME_API Renderer_Impl : public QObject, protected QOpenGLFunctions
{
Q_OBJECT

public:
    explicit Renderer_Impl(QObject* parent = nullptr);
    ~Renderer_Impl();

    void Initialize();
    void Draw();
};

}
