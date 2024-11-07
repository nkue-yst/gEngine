#pragma once

#include "Window_Impl.h"

#include <QOpenGLWidget>
#include <QWindow>

namespace gngin
{

class GENGINE_RUNTIME_API WindowQtGL_Impl : public QOpenGLWidget, public Window_Impl
{
Q_OBJECT

public:
    explicit WindowQtGL_Impl(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    ~WindowQtGL_Impl();

    /* OpenGL Functions */
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void SetWindowTitle(std::string title) override;
    void Update() override;
    bool Open() override;

    bool IsRunning() override;
};

}  // namespace gngin
