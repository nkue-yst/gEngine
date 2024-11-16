#include "GnMainWindow.h"
#include "./ui_GnMainWindow.h"

#include "PIEWidget.h"

GnMainWindow::GnMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::GnMainWindow)
{
    ui->setupUi(this);

    /* DockWidgetの追加 */
    PIEWidget* pie_widget = new PIEWidget(this);
    this->addDockWidget(Qt::DockWidgetArea::LeftDockWidgetArea, pie_widget);
}

GnMainWindow::~GnMainWindow()
{
    delete ui;
}
