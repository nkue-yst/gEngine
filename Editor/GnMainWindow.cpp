#include "GnMainWindow.h"
#include "./ui_GnMainWindow.h"

#include <QToolButton>

GnMainWindow::GnMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::GnMainWindow)
{
    ui->setupUi(this);

    /* メインウィンドウの設定 */
    this->setWindowTitle("gEngine Editor");
    this->setMinimumSize(360, 240);
    this->resize(1280, 720);

    /* ビューポートをセントラルウィジェットとしてセット */
    viewport_widget = new Viewport(this);
    viewport_widget->setContentsMargins(0, 0, 0, 0);
    viewport_widget->setMinimumSize(360, 240);
    this->setCentralWidget(viewport_widget);

    /* ツールバーボタンの追加 */
    QToolButton* pie_begin_button = new QToolButton(this);
    ui->toolBar->addWidget(pie_begin_button);
}

GnMainWindow::~GnMainWindow()
{
    delete ui;
}
