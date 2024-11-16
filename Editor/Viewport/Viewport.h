#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <QOpenGLWidget>

#include "Renderer/Renderer.h"

class Viewport : public QOpenGLWidget
{
Q_OBJECT

public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport();

    /* OpenGL Functions */
    void initializeGL() override;
    void paintGL() override;

private:
    std::unique_ptr<class gngin::Renderer> renderer_;
};

#endif // VIEWPORT_H
