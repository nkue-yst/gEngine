#pragma once

#include "Window.h"

#include <QOpenGLWidget>
#include <QWindow>

#include <string>

namespace gngin
{

class GENGINE_RUNTIME_API Window_Impl : public QOpenGLWidget
{
Q_OBJECT

public:
    explicit Window_Impl(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    ~Window_Impl();

    void SetWindowTitle(std::string title);
    bool Open();
    void Update();

    bool IsRunning();

    /* OpenGL Functions */
    void initializeGL() override;
    void paintGL() override;

protected:
    std::unique_ptr<class Renderer> renderer_;
};

}  // namespace gngin
