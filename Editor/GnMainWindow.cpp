#include "GnMainWindow.h"
#include "./ui_GnMainWindow.h"

GnMainWindow::GnMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::GnMainWindow)
{
    ui->setupUi(this);
}

GnMainWindow::~GnMainWindow()
{
    delete ui;
}
