#ifndef PIEWIDGET_H
#define PIEWIDGET_H

#include <QDockWidget>

namespace Ui {
class PIEWidget;
}

class PIEWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit PIEWidget(QWidget *parent = nullptr);
    ~PIEWidget();

private:
    Ui::PIEWidget *ui;
};

#endif // PIEWIDGET_H
