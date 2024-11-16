#pragma once

#include "gn_dll.h"

#include <memory>
#include <string>

#include <QOpenGLWidget>
#include <QWindow>

namespace gngin
{

/* ゲームアプリ用ウィンドウクラス */
class GENGINE_RUNTIME_API Window : public QOpenGLWidget
{
Q_OBJECT

public:
    explicit Window(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    ~Window();

    void SetWindowTitle(std::string title);
    bool Open();
    void Update();

    bool IsRunning();

    /* OpenGL Functions */
    void initializeGL() override;
    void paintGL() override;

private:
    std::unique_ptr<class Renderer> renderer_;
};

}  // namespace gngin
