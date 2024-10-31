#ifndef GNMAINWINDOW_H
#define GNMAINWINDOW_H

#include <QMainWindow>

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
};
#endif // GNMAINWINDOW_H
