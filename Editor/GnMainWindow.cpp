#include "GnMainWindow.h"
#include "./ui_GnMainWindow.h"

#include "gengine.h"
#include "Classes/GameInstance/GameInstance.h"

#include "Application/Application.h"
#include "Window/Window.h"

#include <QAction>
#include <QPixMap>

using namespace gngin;

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
    if (old_central_widget)
    {
        old_central_widget->setParent(nullptr);
    }

    /* アプリをPIEモードで実行 */
    Engine::InitializeApplication(0, nullptr, true);
    GameInstance game_instance;
    game_instance.SetTitle("SampleApplication_PIE");

    Engine::RegisterGameInstance(&game_instance);
    Engine::LaunchApplication();

    auto* window_ref = Engine::GetApplicationRef()->GetWindowRef();
    window_ref->setParent(this);
    this->setCentralWidget(window_ref);
}

void GnMainWindow::StopPlayInEditor()
{
    this->setCentralWidget(viewport_widget);

    /* PIEモードで実行中のアプリを停止 */
    Engine::InitializeApplication(0, nullptr, true);
}
