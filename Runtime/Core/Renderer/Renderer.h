#pragma once

#include "gn_dll.h"

#include <memory>

#include <QObject>
#include <QOpenGLFunctions>

namespace gngin
{

class GENGINE_RUNTIME_API Renderer : public QObject, protected QOpenGLFunctions
{
Q_OBJECT

public:
    explicit Renderer(QObject* parent = nullptr);
    ~Renderer();

    void Initialize();
    void Draw();
};

}  // namespace gngin
