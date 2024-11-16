#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <QOpenGLWidget>

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
    std::unique_ptr<class ViewportRenderer> renderer_;
};

#endif // VIEWPORT_H
