#include "PIEWidget.h"
#include "ui_PIEWidget.h"

PIEWidget::PIEWidget(QWidget *parent)
    : QDockWidget(parent)
    , ui(new Ui::PIEWidget)
{
    ui->setupUi(this);
}

PIEWidget::~PIEWidget()
{
    delete ui;
}
