#include "GnMainWindow.h"
#include "./ui_GnMainWindow.h"

#include <QAction>
#include <QPixMap>

GnMainWindow::GnMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::GnMainWindow)
    , dummy_viewport(new QWidget(this))
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
    /* PIEの開始 / 停止ボタン */
    QAction* pie_begin_button = new QAction(this);
    pie_begin_button->setCheckable(true);

    // アイコンの設定
    QIcon icon;
    icon.addPixmap(QPixmap(":/UI/Play"), QIcon::Normal, QIcon::Off);
    icon.addPixmap(QPixmap(":/UI/Stop"), QIcon::Normal, QIcon::On);
    pie_begin_button->setIcon(icon);

    QObject::connect(pie_begin_button, &QAction::toggled, this, [this](bool checked)
    {
        this->TogglePlayInEditor(checked);
    });
    ui->toolBar->addAction(pie_begin_button);
}

GnMainWindow::~GnMainWindow()
{
    delete ui;
}

void GnMainWindow::TogglePlayInEditor(bool checked)
{
    if (checked)
    {
        this->BeginPlayInEditor();
    }
    else
    {
        this->StopPlayInEditor();
    }
}

void GnMainWindow::BeginPlayInEditor()
{
    QWidget* old_central_widget = this->takeCentralWidget();
    old_central_widget->setParent(nullptr);

    this->setCentralWidget(dummy_viewport);
}

void GnMainWindow::StopPlayInEditor()
{
    QWidget* old_central_widget = this->takeCentralWidget();
    old_central_widget->setParent(nullptr);

    this->setCentralWidget(viewport_widget);
}
