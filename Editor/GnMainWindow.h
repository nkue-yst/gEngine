#ifndef GNMAINWINDOW_H
#define GNMAINWINDOW_H

#include <QMainWindow>

#include "Viewport.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class GnMainWindow;
}
QT_END_NAMESPACE

class GnMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    GnMainWindow(QWidget *parent = nullptr);
    ~GnMainWindow();

private:
    Ui::GnMainWindow *ui;

    Viewport* viewport_widget;
};
#endif // GNMAINWINDOW_H
