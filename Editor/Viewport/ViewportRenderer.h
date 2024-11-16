#pragma once

#include <QObject>
#include <QOpenGLFunctions>

class ViewportRenderer : public QObject, protected QOpenGLFunctions
{
Q_OBJECT

public:
    explicit ViewportRenderer(QObject* parent = nullptr);
    ~ViewportRenderer();

    void Initialize();
    void Draw();
};
