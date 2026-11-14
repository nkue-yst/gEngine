#include "editor_core.hpp"
#include "json.hpp"
#include "mcp.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <charconv>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <sstream>
#include <windowsx.h>

using namespace gengine::editor;

namespace
{
    enum Control : int
    {
        kPath = 100,
        kCreate,
        kOpen,
        kBrowse,
        kClose,
        kSources,
        kCompile,
        kCancel,
        kEdit,
        kStatus,
        kDiagnostics,
        kProject,
        kWorkspace,
        kLoadVm,
        kUnloadVm,
        kFunction,
        kCall,
        kReset,
        kVmStatus,
        kInspector,
        kPool,
        kColumns,
        kOffset,
        kLimit,
        kInspect,
        kSnapshot,
        kMetrics,
        kMetricsText,
        kRefreshMetrics,
        kFuel,
        kOwnedLimit,
        kPlay,
        kPausePlay,
        kStopPlay,
        kResetPlay,
        kNormalPlay,
        kPreset,
        kPlayOptions,
        kGameMode,
        kTemplate,
        kSceneList,
        kSceneOpen,
        kObjects,
        kObjectKind,
        kAddObject,
        kDeleteObject,
        kApplyProperties,
        kSaveScene,
        kReloadScene,
        kUndo,
        kRedo,
        kSceneStatus,
        kRecovery,
        kSceneDiagnostics,
        kPropertyRefresh,
        kSceneError,
        kTechnical,
        kFitView,
        kAssets,
        kAssetPath,
        kAssetRegister,
        kAssetBrowse,
        kAssetRefresh,
        kAssetList,
        kAssetDetail,
        kAssetTarget,
        kAssetSlot,
        kAssetAssign,
        kAssetClear,
        kAssetIssues,
        kBuild,
        kPlayMode,
        kRunnerStatus
    };

    class EditorWindow : public QMainWindow
    {
        friend class QtEditorTest;

      public:
        EditorWindow(std::filesystem::path workspace, std::filesystem::path compiler, bool mcp,
                     bool automatic_workspace)
            : QMainWindow(nullptr), core_(std::move(workspace), std::move(compiler), automatic_workspace),
              adapter_(core_), mcp_(mcp), automatic_workspace_(automatic_workspace)
        {
            if (mcp_) {
                transport_ = std::make_unique<StdioTransport>();
            }
        }

        int Run()
        {
            window_ = reinterpret_cast<HWND>(winId());
            setWindowTitle(QStringLiteral("gEngine Editor " GENGINE_VERSION));
            resize(1100, 850);
            setMinimumSize(960, 800);
            Initialize();
            InitializePerformance();
            menuBar()->addMenu(QString::fromUtf8("ヘルプ"))->addAction(QString::fromUtf8("Qt について"), this, [this] {
                QMessageBox::aboutQt(this);
            });
            core_.SetPlayParent(reinterpret_cast<HWND>(view_host_->winId()));
            connect(&timer_, &QTimer::timeout, this, [this] {
                const auto callback_begin = std::chrono::steady_clock::now();
                try {
                    if (transport_) {
                        if (transport_->Disconnected()) {
                            shutting_down_ = true;
                            core_.Shutdown();
                            QApplication::quit();
                            return;
                        }
                        auto responses = adapter_.Receive(transport_->Take());
                        if (responses.empty() == false) {
                            // 操作結果を返す前に、子ビューの配置とGUIの状態を一致させる。
                            Refresh();
                            centralWidget()->layout()->activate();
                            ResizeView();
                        }
                        for (auto& response : responses) {
                            transport_->Send(std::move(response));
                        }
                    }
                    core_.Tick();
                    SyncViewportControls();
                    if (++ticks_ % 10 == 0) {
                        Refresh();
                    }
                    ResizeView();
                } catch (const std::exception& error) {
                    ShowError(error.what());
                }
                core_.RecordHostTime(gengine::Timing::kEditor,
                                     static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                    std::chrono::steady_clock::now() - callback_begin)
                                                                    .count()));
            });
            timer_.start(20);
            Refresh();
            show();
            const auto result = QApplication::exec();
            timer_.stop();
            if (transport_) {
                transport_->Stop();
            }
            core_.Shutdown();
            return result;
        }

      protected:
        void closeEvent(QCloseEvent* event) override
        {
            // 通信切断は復旧入力を残して終了し、無人起動を保存ダイアログで止めない。
            if (shutting_down_) {
                event->accept();
                return;
            }
            if (AllowPlacementInput() == false) {
                event->ignore();
                return;
            }
            const auto choice = UnsavedChoice();
            const auto result = core_.scenes().ResolveUnsaved(choice, document_state_.revision);
            if (result.has_value() == false || result->status == "Cancelled") {
                event->ignore();
                return;
            }
            if (choice == "discard") {
                core_.scenes().Detach();
            }
            core_.Shutdown();
            event->accept();
        }

      private:
        void InitializePerformance()
        {
            auto* dock = new QDockWidget(QString::fromUtf8("ゲーム性能"), this);
            dock->setObjectName("performanceDock");
            auto* body = new QWidget;
            auto* layout = new QVBoxLayout(body);
            auto* conditions = new QComboBox;
            conditions->setObjectName("performanceSamples");
            for (const auto count : {120, 300, 600}) {
                conditions->addItem(QString::fromUtf8("30 フレーム準備 / %1 標本").arg(count), count);
            }
            conditions->setCurrentIndex(1);
            layout->addWidget(conditions);
            auto* row = new QHBoxLayout;
            auto* output = new QPlainTextEdit;
            output->setObjectName("performanceReport");
            output->setReadOnly(true);
            output->setPlainText(QString::fromUtf8("PIE を開始して計測してください。結果は明示更新します。"));
            for (const auto& entry :
                 std::array<std::pair<const char*, const char*>, 3>{{{"performance.start", "開始"},
                                                                     {"performance.get", "結果を更新"},
                                                                     {"performance.stop", "中止"}}}) {
                auto* button = new QPushButton(QString::fromUtf8(entry.second));
                button->setObjectName(entry.first);
                row->addWidget(button);
                connect(
                    button, &QPushButton::clicked, this, [this, conditions, output, action = std::string(entry.first)] {
                        const auto began = std::chrono::steady_clock::now();
                        const auto play = core_.PlayStatus();
                        if (play.play_id.empty()) {
                            output->setPlainText(QString::fromUtf8(
                                "PIE を開始してください。単独実行の計測は GameRunner の --metrics を使います。"));
                            return;
                        }
                        const auto report =
                            action == "performance.start"
                                ? Invoke<gengine::PerformanceReport>(
                                      action.c_str(),
                                      PerformanceStart{play.play_id, {30, conditions->currentData().toUInt()}}, true)
                                : Invoke<gengine::PerformanceReport>(action.c_str(), PlayRequest{play.play_id},
                                                                     action == "performance.stop");
                        if (report) {
                            auto text =
                                QString::fromStdString(report->source + " / " + report->game + " / " + report->state);
                            text += QString::fromUtf8("\n標本 %1 / %2\n時間：平均 / p95 / p99（ms）\n")
                                        .arg(report->collected)
                                        .arg(report->config.samples);
                            for (const auto& timing : report->timings) {
                                text += QString::fromStdString(timing.name) + ": ";
                                text += timing.samples == 0 ? QString::fromUtf8("未計測\n")
                                                            : QString("%1 / %2 / %3 (%4)\n")
                                                                  .arg(timing.mean_ns / 1e6, 0, 'f', 3)
                                                                  .arg(double(timing.p95_ns) / 1e6, 0, 'f', 3)
                                                                  .arg(double(timing.p99_ns) / 1e6, 0, 'f', 3)
                                                                  .arg(timing.samples);
                            }
                            text += QString::fromUtf8("\nプロセス Private Bytes: %1\n")
                                        .arg(report->process_end.private_bytes);
                            for (const auto& counter : report->counters) {
                                text +=
                                    QString::fromStdString(counter.name) + ": " + QString::number(counter.value) + "\n";
                            }
                            text += QString::fromUtf8(
                                "\nrender_present は待機込み。GPU は完了済みの別標本。\n"
                                "interval は開始間隔、scheduler_lateness は予定からの遅延。\n"
                                "present_cpu は提示呼出しのCPU時間。dxgi_present_interval は有効な連続提示だけ。\n"
                                "editor_callback はタイマー処理、metrics_display は取得・整形・表示要求。\n"
                                "OS のピークはプロセス生存期間、部品の予約量はプロセス全体ではありません。");
                            output->setPlainText(text);
                        }
                        core_.RecordHostTime(
                            gengine::Timing::kDisplay,
                            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                           std::chrono::steady_clock::now() - began)
                                                           .count()));
                    });
            }
            layout->addLayout(row);
            layout->addWidget(output);
            dock->setWidget(body);
            addDockWidget(Qt::RightDockWidgetArea, dock);
            dock->hide();
            menuBar()->addMenu(QString::fromUtf8("計測"))->addAction(dock->toggleViewAction());
        }

        bool EditingEnabled() const
        {
            return core_.PlayHandle() == nullptr && core_.RunnerActive() == false &&
                   state_.active_job.has_value() == false;
        }

        QWidget* Control(int id) const
        {
            return controls_.at(id);
        }
        static void SetText(QWidget* control, const wchar_t* text)
        {
            const auto value = QString::fromWCharArray(text);
            if (auto* edit = qobject_cast<QLineEdit*>(control)) {
                if (edit->text() != value) {
                    edit->setText(value);
                }
            }
            else if (auto* label = qobject_cast<QLabel*>(control)) {
                if (label->text() != value) {
                    label->setText(value);
                }
            }
            else if (auto* button = qobject_cast<QPushButton*>(control)) {
                if (button->text() != value) {
                    button->setText(value);
                }
            }
            else if (auto* output = qobject_cast<QPlainTextEdit*>(control)) {
                if (output->toPlainText() != value) {
                    output->setPlainText(value);
                }
            }
        }
        static void Enable(QWidget* control, bool enabled)
        {
            control->setEnabled(enabled);
        }
        static void Focus(QWidget* control)
        {
            control->setFocus();
        }
        int Index(int id) const
        {
            if (auto* list = qobject_cast<QListWidget*>(Control(id))) {
                return list->currentRow();
            }
            return qobject_cast<QComboBox*>(Control(id))->currentIndex();
        }
        void SelectIndex(int id, int index)
        {
            const auto block = QSignalBlocker(Control(id));
            if (auto* list = qobject_cast<QListWidget*>(Control(id))) {
                list->setCurrentRow(index);
            }
            else {
                qobject_cast<QComboBox*>(Control(id))->setCurrentIndex(index);
            }
        }
        void ClearItems(int id)
        {
            const auto block = QSignalBlocker(Control(id));
            if (auto* list = qobject_cast<QListWidget*>(Control(id))) {
                list->clear();
            }
            else {
                qobject_cast<QComboBox*>(Control(id))->clear();
            }
        }
        void AddItem(int id, const wchar_t* text)
        {
            const auto block = QSignalBlocker(Control(id));
            if (auto* list = qobject_cast<QListWidget*>(Control(id))) {
                list->addItem(QString::fromWCharArray(text));
            }
            else {
                qobject_cast<QComboBox*>(Control(id))->addItem(QString::fromWCharArray(text));
            }
        }
        void Add(int id, const wchar_t* type, const wchar_t* text, DWORD style = 0)
        {
            auto* widget = static_cast<QWidget*>(nullptr);
            const auto kind = std::wstring_view(type);
            if (kind == L"BUTTON") {
                auto* button = new QPushButton;
                connect(button, &QPushButton::clicked, this, [this, id] {
                    try {
                        Command(id);
                    } catch (const std::exception& error) {
                        ShowError(error.what());
                    }
                });
                widget = button;
            }
            else if (kind == L"COMBOBOX") {
                widget = new QComboBox;
            }
            else if (kind == L"LISTBOX") {
                widget = new QListWidget;
            }
            else if (kind == L"EDIT" && (style & ES_MULTILINE) != 0) {
                auto* output = new QPlainTextEdit;
                output->setReadOnly(true);
                widget = output;
            }
            else if (kind == L"EDIT") {
                auto* edit = new QLineEdit;
                connect(edit, &QLineEdit::textEdited, this, [this, id] {
                    if (id >= 1001 && id <= 1031) {
                        form_dirty_ = true;
                    }
                });
                widget = edit;
            }
            else {
                auto* label = new QLabel;
                label->setWordWrap(true);
                widget = label;
            }
            widget->setParent(this);
            widget->setObjectName(QStringLiteral("control_%1").arg(id));
            controls_[id] = widget;
            SetText(widget, text);
        }

        void BuildLayout()
        {
            auto* central = new QWidget;
            auto* root = new QVBoxLayout(central);
            setCentralWidget(central);
            const auto row = [this](QVBoxLayout* parent, std::initializer_list<int> ids) {
                auto* line = new QHBoxLayout;
                for (const auto id : ids) {
                    line->addWidget(Control(id));
                }
                parent->addLayout(line);
            };
            root->addWidget(Control(kProject));
            root->addWidget(Control(kWorkspace));
            row(root, {kPath, kTemplate, kCreate, kOpen, kBrowse, kClose});
            auto* sources = qobject_cast<QListWidget*>(Control(kSources));
            sources->setMaximumHeight(64);
            root->addWidget(sources);
            row(root, {kEdit, kCompile, kBuild, kCancel, kPlayMode, kPlay, kStopPlay, kPlayOptions, kTechnical});
            row(root, {kRunnerStatus});
            row(root, {kPreset, kGameMode, kPausePlay, kResetPlay, kNormalPlay});
            pages_ = new QStackedWidget;
            root->addWidget(pages_, 1);
            auto* scene_page = new QWidget;
            auto* scene_layout = new QVBoxLayout(scene_page);
            row(scene_layout, {kSceneList, kSceneOpen, kSaveScene, kReloadScene, kUndo, kRedo, kRecovery});
            scene_layout->addWidget(Control(kSceneStatus));
            auto* panels = new QHBoxLayout;
            scene_layout->addLayout(panels, 1);
            auto* left = new QVBoxLayout;
            Control(kObjects)->setFixedWidth(180);
            left->addWidget(Control(kObjects));
            left->addWidget(Control(kObjectKind));
            row(left, {kAddObject, kDeleteObject});
            left->addWidget(Control(kAssets));
            panels->addLayout(left);
            auto* center = new QVBoxLayout;
            view_host_ = new QWidget;
            view_host_->setAttribute(Qt::WA_NativeWindow);
            view_host_->setFocusPolicy(Qt::StrongFocus);
            view_host_->setMinimumSize(200, 200);
            center->addWidget(view_host_, 1);
            center->addWidget(Control(kFitView));
            panels->addLayout(center, 1);
            auto* right = new QVBoxLayout;
            auto* scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFixedWidth(300);
            auto* form = new QWidget;
            auto* fields = new QFormLayout(form);
            for (auto i = 0; i < 16; ++i) {
                fields->addRow(Control(1000 + i * 2), Control(1001 + i * 2));
                Control(1001 + i * 2)->setAccessibleName(QStringLiteral("property_%1").arg(i));
            }
            scroll->setWidget(form);
            right->addWidget(scroll, 1);
            right->addWidget(Control(kSceneError));
            right->addWidget(Control(kAssetIssues));
            row(right, {kApplyProperties, kPropertyRefresh});
            panels->addLayout(right);
            pages_->addWidget(scene_page);
            auto* technical = new QWidget;
            auto* details = new QVBoxLayout(technical);
            auto* execution_fields = new QFormLayout;
            execution_fields->addRow(Control(208), Control(kOwnedLimit));
            execution_fields->addRow(Control(203), Control(kFunction));
            execution_fields->addRow(Control(207), Control(kFuel));
            details->addLayout(execution_fields);
            row(details, {kLoadVm, kUnloadVm, kCall, kReset});
            details->addWidget(Control(kVmStatus));
            row(details, {kInspector, kMetrics, kRefreshMetrics});
            inspector_fields_ = new QWidget;
            auto* inspection = new QFormLayout(inspector_fields_);
            inspection->addRow(Control(204), Control(kPool));
            inspection->addRow(Control(205), Control(kColumns));
            auto* range = new QHBoxLayout;
            range->addWidget(Control(kOffset));
            range->addWidget(Control(kLimit));
            range->addWidget(Control(kInspect));
            inspection->addRow(Control(206), range);
            details->addWidget(inspector_fields_);
            details->addWidget(Control(kSnapshot), 1);
            details->addWidget(Control(kMetricsText), 1);
            pages_->addWidget(technical);
            auto* assets = new QWidget;
            auto* asset_layout = new QVBoxLayout(assets);
            row(asset_layout, {kAssetPath, kAssetBrowse, kAssetRegister, kAssetRefresh});
            asset_layout->addWidget(Control(kAssetList), 1);
            asset_layout->addWidget(Control(kAssetDetail));
            asset_layout->addWidget(Control(kAssetTarget));
            row(asset_layout, {kAssetSlot, kAssetAssign, kAssetClear});
            auto* back = new QPushButton(QString::fromUtf8("シーン編集へ戻る"));
            connect(back, &QPushButton::clicked, this, [this] {
                assets_open_ = false;
                Layout();
            });
            asset_layout->addWidget(back);
            pages_->addWidget(assets);
            connect(qobject_cast<QListWidget*>(Control(kAssetList)), &QListWidget::currentRowChanged, this,
                    [this](int index) {
                        if (index >= 0 && std::size_t(index) < asset_catalog_.entries.size()) {
                            const auto& entry = asset_catalog_.entries[index];
                            SetText(Control(kAssetDetail),
                                    Wide(entry.path + "\n" + entry.summary + "\n" + entry.diagnostic).c_str());
                        }
                    });
            Control(kDiagnostics)->setMaximumHeight(100);
            root->addWidget(Control(kStatus));
            root->addWidget(Control(kDiagnostics));
            // 旧画面の補助ラベルは移行先のレイアウトに含まれるものだけ表示する。
            for (const auto& [id, widget] : controls_) {
                if (widget->parentWidget() == this) {
                    widget->hide();
                }
            }
            // ラベルは対応する入力のアクセシブル名にも反映する。
            connect(qobject_cast<QListWidget*>(Control(kObjects)), &QListWidget::currentRowChanged, this,
                    [this](int index) {
                        if (AllowPlacementInput() == false) {
                            const auto found = std::find(object_ids_.begin(), object_ids_.end(), selected_object_);
                            SelectIndex(kObjects, found == object_ids_.end() ? 0 : int(found - object_ids_.begin()));
                            return;
                        }
                        if (index >= 0 && std::size_t(index) < object_ids_.size()) {
                            selected_object_ = object_ids_[index];
                            core_.SelectViewportObject(selected_object_);
                            BindProperties();
                            Layout();
                        }
                    });
        }
        void ResizeView()
        {
            const auto view = core_.SceneViewHandle();
            const auto play_changed = laid_out_play_ != core_.PlayHandle();
            laid_out_view_ = view;
            laid_out_play_ = core_.PlayHandle();
            if (view == nullptr) {
                return;
            }
            auto rect = RECT{};
            GetClientRect(reinterpret_cast<HWND>(view_host_->winId()), &rect);
            auto current = RECT{};
            GetClientRect(view, &current);
            if (current.right != rect.right || current.bottom != rect.bottom) {
                MoveWindow(view, 0, 0, rect.right, rect.bottom, TRUE);
            }
            if ((IsWindowVisible(view) != FALSE) == (technical_open_ || assets_open_)) {
                ShowWindow(view, (technical_open_ || assets_open_) ? SW_HIDE : SW_SHOW);
            }
            if (play_changed && core_.PlayHandle() && GetFocus() != view) {
                // Qtの部品更新によるフォーカス移動を終えてから、試遊へ入力を渡す。
                SetFocus(view);
            }
        }
        void Layout()
        {
            pages_->setCurrentIndex(assets_open_ ? 2 : technical_open_ ? 1 : 0);
            SetText(Control(kTechnical), technical_open_ ? L"シーン編集へ戻る" : L"VM 詳細・計測");
            for (const auto id : {kPreset, kGameMode, kPausePlay, kResetPlay, kNormalPlay}) {
                Control(id)->setVisible(play_options_open_);
            }
            Control(kSnapshot)->setVisible(inspector_open_);
            inspector_fields_->setVisible(inspector_open_);
            Control(kMetricsText)->setVisible(metrics_open_);
            for (const auto id : {kPool, kColumns, kOffset, kLimit, kInspect}) {
                Control(id)->setVisible(inspector_open_);
            }
            const auto playing = core_.PlayHandle() != nullptr || core_.RunnerActive() || state_.active_job.has_value();
            for (auto i = 0u; i < 16; ++i) {
                Control(1000 + int(i) * 2)->setVisible(i < property_fields_.size());
                Control(1001 + int(i) * 2)->setVisible(i < property_fields_.size());
            }
            for (const auto id : {kSceneOpen, kAddObject, kDeleteObject, kApplyProperties, kReloadScene, kRecovery}) {
                Enable(Control(id),
                       playing == false && (id == kSceneOpen || document_state_.document_id.empty() == false));
            }
            Enable(Control(kUndo), playing == false && document_state_.undo_id.has_value());
            Enable(Control(kRedo), playing == false && document_state_.redo_id.has_value());
            Enable(Control(kPlay),
                   playing == false && (Index(kPlayMode) == 1 ? state_.project.has_value() && core_.scenes().document()
                                                              : state_.last_job.has_value()));
            Enable(Control(kBuild), playing == false && state_.project.has_value() && core_.scenes().document());
            Enable(Control(kPlayMode), playing == false);
            for (const auto id : {kPausePlay, kStopPlay, kResetPlay, kNormalPlay}) {
                Enable(Control(id), core_.PlayHandle() != nullptr);
            }
            Enable(Control(kStopPlay), core_.PlayHandle() != nullptr || core_.RunnerActive());
            Control(kFitView)->setVisible(core_.scenes().document() && playing == false);
            SyncViewportControls();
            ResizeView();
        }

        void Initialize()
        {
            Add(kProject, L"STATIC", L"プロジェクトを作成するか、開いてください");
            const auto workspace =
                L"作業フォルダー: " + core_.workspace().wstring() + (mcp_ ? L"   /   MCP 接続" : L"");
            Add(kWorkspace, L"STATIC", workspace.c_str(), SS_PATHELLIPSIS);
            Add(200, L"STATIC", L"新規フォルダー名 / プロジェクト設定ファイル");
            Add(kPath, L"EDIT", L"MyProject", WS_TABSTOP | ES_AUTOHSCROLL);
            qobject_cast<QLineEdit*>(Control(kPath))->setMaxLength(1024);
            Add(kCreate, L"BUTTON", L"新規作成", WS_TABSTOP);
            Add(kOpen, L"BUTTON", L"開く", WS_TABSTOP);
            Add(kBrowse, L"BUTTON", L"参照…", WS_TABSTOP);
            Add(kClose, L"BUTTON", L"閉じる", WS_TABSTOP);

            // 左側でソースを選び、右側でコンパイル操作と結果の確認を行う。
            Add(201, L"STATIC", L"ソースファイル");
            Add(kSources, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT);
            Add(kEdit, L"BUTTON", L"メモ帳で編集", WS_TABSTOP);
            Add(kCompile, L"BUTTON", L"コンパイル", WS_TABSTOP | BS_DEFPUSHBUTTON);
            Add(kBuild, L"BUTTON", L"ビルド", WS_TABSTOP);
            Add(kPlayMode, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST);
            AddItem(kPlayMode, L"PIE");
            AddItem(kPlayMode, L"スタンドアロン");
            SelectIndex(kPlayMode, 0);
            connect(qobject_cast<QComboBox*>(Control(kPlayMode)), &QComboBox::currentIndexChanged, this, [this] {
                Layout();
            });
            Add(kRunnerStatus, L"STATIC", L"");
            Add(kPlay, L"BUTTON", L"実行", WS_TABSTOP);
            Add(kPausePlay, L"BUTTON", L"停止 / 再開", WS_TABSTOP);
            Add(kStopPlay, L"BUTTON", L"試遊終了", WS_TABSTOP);
            Add(kPlayOptions, L"BUTTON", L"試遊条件", WS_TABSTOP);
            Add(kResetPlay, L"BUTTON", L"再初期化", WS_TABSTOP);
            Add(kNormalPlay, L"BUTTON", L"通常開始", WS_TABSTOP);
            Add(kPreset, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST);
            for (const auto* name : {L"通常開始", L"プレイ開始", L"クリア直前", L"失敗確認", L"弾幕負荷"}) {
                AddItem(kPreset, name);
            }
            SelectIndex(kPreset, int(0));
            Add(kGameMode, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST);
            for (const auto* name : {L"2D ゲーム", L"3D ゲーム"}) {
                AddItem(kGameMode, name);
            }
            SelectIndex(kGameMode, int(0));
            Add(kCancel, L"BUTTON", L"取消し", WS_TABSTOP);
            Add(kStatus, L"STATIC", L"準備完了");
            Add(202, L"STATIC", L"診断・成果物");
            Add(kDiagnostics, L"EDIT", L"",
                WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL);

            // VM操作は表示を閉じても残す。集合表示が実行やresetを妨げないようにする。
            Add(kLoadVm, L"BUTTON", L"VMへ読込み", WS_TABSTOP);
            Add(kUnloadVm, L"BUTTON", L"VMを解放", WS_TABSTOP);
            Add(203, L"STATIC", L"公開関数（引数・戻り値なし）");
            Add(kFunction, L"EDIT", L"initialize", WS_TABSTOP | ES_AUTOHSCROLL);
            Add(kCall, L"BUTTON", L"一回実行", WS_TABSTOP);
            Add(kReset, L"BUTTON", L"リセット", WS_TABSTOP);
            Add(kVmStatus, L"STATIC", L"VM未読込み");
            Add(kInspector, L"BUTTON", L"集合を表示", WS_TABSTOP);
            Add(204, L"STATIC", L"集合名");
            Add(kPool, L"EDIT", L"particles2d", WS_TABSTOP | ES_AUTOHSCROLL);
            Add(205, L"STATIC", L"列名:成分（カンマ区切り、最大8列）");
            Add(kColumns, L"EDIT", L"position:0,position:1,life,serial,active", WS_TABSTOP | ES_AUTOHSCROLL);
            Add(206, L"STATIC", L"先頭行 / 件数（最大64）");
            Add(kOffset, L"EDIT", L"0", WS_TABSTOP | ES_NUMBER);
            Add(kLimit, L"EDIT", L"16", WS_TABSTOP | ES_NUMBER);
            Add(kInspect, L"BUTTON", L"表示を更新", WS_TABSTOP);
            Add(kSnapshot, L"EDIT", L"",
                WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL);
            Add(kMetrics, L"BUTTON", L"計測・予算", WS_TABSTOP);
            Add(207, L"STATIC", L"実行 fuel（1～1,000,000）");
            Add(kFuel, L"EDIT", L"1000000", WS_TABSTOP | ES_NUMBER);
            Add(208, L"STATIC", L"次回読込みの所有量上限（bytes）");
            Add(kOwnedLimit, L"EDIT", L"67108864", WS_TABSTOP | ES_NUMBER);
            Add(kRefreshMetrics, L"BUTTON", L"計測を更新", WS_TABSTOP);
            Add(kMetricsText, L"EDIT", L"",
                WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL);
            for (const auto id : {kFuel, kOwnedLimit}) {
                qobject_cast<QLineEdit*>(Control(id))->setMaxLength(10);
            }
            for (const auto id : {kFunction, kPool}) {
                qobject_cast<QLineEdit*>(Control(id))->setMaxLength(128);
            }
            // 半角の列名と値を等幅で揃え、見出しと各個体の値の対応を保つ。
            qobject_cast<QLineEdit*>(Control(kColumns))->setMaxLength(1100);
            qobject_cast<QLineEdit*>(Control(kOffset))->setMaxLength(10);
            qobject_cast<QLineEdit*>(Control(kLimit))->setMaxLength(2);

            Add(kTemplate, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST);
            for (const auto* text : {L"2D ゲーム", L"3D ゲーム", L"VM 検証"}) {
                AddItem(kTemplate, text);
            }
            SelectIndex(kTemplate, int(0));
            Add(kSceneList, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST);
            Add(kSceneOpen, L"BUTTON", L"シーンを開く", WS_TABSTOP);
            Add(kObjects, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL);
            Add(kObjectKind, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST);
            for (const auto* text : {L"プレイヤー", L"敵", L"カメラ"}) {
                AddItem(kObjectKind, text);
            }
            SelectIndex(kObjectKind, int(0));
            Add(kAddObject, L"BUTTON", L"追加", WS_TABSTOP);
            Add(kDeleteObject, L"BUTTON", L"削除", WS_TABSTOP);
            Add(kApplyProperties, L"BUTTON", L"値を適用", WS_TABSTOP);
            Add(kTechnical, L"BUTTON", L"VM 詳細・計測", WS_TABSTOP);
            Add(kSceneError, L"STATIC", L"");
            Add(kPropertyRefresh, L"BUTTON", L"値を読み直す", WS_TABSTOP);
            Add(kSaveScene, L"BUTTON", L"保存", WS_TABSTOP);
            Add(kReloadScene, L"BUTTON", L"再読込み", WS_TABSTOP);
            Add(kUndo, L"BUTTON", L"元に戻す", WS_TABSTOP);
            Add(kRedo, L"BUTTON", L"やり直す", WS_TABSTOP);
            Add(kSceneStatus, L"STATIC", L"シーンを開いてください");
            Add(kRecovery, L"BUTTON", L"復旧入力", WS_TABSTOP);
            Add(kSceneDiagnostics, L"BUTTON", L"診断", WS_TABSTOP);
            Add(kFitView, L"BUTTON", L"全体表示", WS_TABSTOP);
            Add(kAssets, L"BUTTON", L"素材一覧", WS_TABSTOP);
            Add(kAssetPath, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL);
            qobject_cast<QLineEdit*>(Control(kAssetPath))
                ->setPlaceholderText(QString::fromUtf8("プロジェクト内の素材パス"));
            Control(kAssetPath)->setAccessibleName(QString::fromUtf8("登録する素材の相対パス"));
            Add(kAssetBrowse, L"BUTTON", L"参照…", WS_TABSTOP);
            Add(kAssetRegister, L"BUTTON", L"登録", WS_TABSTOP);
            Add(kAssetRefresh, L"BUTTON", L"再検証", WS_TABSTOP);
            Add(kAssetList, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL);
            Control(kAssetList)->setAccessibleName(QString::fromUtf8("登録素材"));
            Add(kAssetDetail, L"STATIC", L"");
            qobject_cast<QLabel*>(Control(kAssetDetail))->setWordWrap(true);
            Add(kAssetTarget, L"STATIC", L"シーンでプレイヤーまたは敵を選んでください");
            Add(kAssetSlot, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST);
            AddItem(kAssetSlot, L"描画素材");
            AddItem(kAssetSlot, L"音声素材");
            SelectIndex(kAssetSlot, 0);
            Add(kAssetAssign, L"BUTTON", L"選択素材を割当て", WS_TABSTOP);
            Add(kAssetClear, L"BUTTON", L"標準素材に戻す", WS_TABSTOP);
            Add(kAssetIssues, L"STATIC", L"");
            qobject_cast<QLabel*>(Control(kAssetIssues))->setWordWrap(true);
            for (auto i = 0; i < 16; ++i) {
                Add(1000 + i * 2, L"STATIC", L"");
                Add(1001 + i * 2, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL);
                qobject_cast<QLineEdit*>(Control(1001 + i * 2))->setMaxLength(128);
            }
            BuildLayout();
            Layout();
        }
        template <class Output, class Input>
        std::optional<Output> Invoke(std::string id, const Input& request, bool mutation = false)
        {
            // GUIからも共通Dispatcherへ要求する。変更操作にはクリックごとの操作IDを付ける。
            auto& dispatcher = core_.dispatcher();
            const auto accepted =
                dispatcher.SubmitTyped(id, request, mutation ? "gui-" + std::to_string(++sequence_) : "");
            if (accepted.has_value() == false) {
                ShowError(accepted.error().code + ": " + accepted.error().reason);
                return {};
            }

            // 受付済みの操作を実行して結果を取り出す。コンパイル本体の完了はここでは待たない。
            dispatcher.Pump();
            const auto operation = dispatcher.Lookup(accepted->operation_id).operation;
            if (operation->error.has_value()) {
                ShowError(operation->error->code + ": " + operation->error->reason);
                return {};
            }
            return Decode<Output>(operation->result).value();
        }

        void ShowError(const std::string& error)
        {
            SetText(Control(kStatus), L"操作できません");
            const auto display_error = error.starts_with("InvalidArgument: ") ? error.substr(17) : error;
            SetText(Control(kSceneError), Wide(display_error).c_str());
            for (auto row = 0u; row < property_fields_.size(); ++row) {
                if (error.find(property_fields_[row].key) != std::string::npos) {
                    Focus(Control(1001 + int(row) * 2));
                    break;
                }
            }
            SetText(Control(kDiagnostics), Wide(error).c_str());
            if (inspector_open_) {
                SetText(Control(kSnapshot), Wide(error).c_str());
            }
            if (metrics_open_) {
                SetText(Control(kMetricsText), Wide(error).c_str());
            }
        }

        std::string InputText(int id = kPath) const
        {
            const auto* control = Control(id);
            if (const auto* edit = qobject_cast<const QLineEdit*>(control)) {
                return edit->text().toStdString();
            }
            return qobject_cast<const QPlainTextEdit*>(control)->toPlainText().toStdString();
        }

        std::uint32_t ReadNumber(std::string_view text) const
        {
            auto value = std::uint32_t{};
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
                throw std::invalid_argument("数値欄には0以上の整数を指定してください。上限は各項目に従います。");
            }
            return value;
        }

        void Inspect()
        {
            if (state_.vm_id.has_value() == false) {
                return;
            }
            auto request = PoolRequest{
                *state_.vm_id, InputText(kPool), {}, ReadNumber(InputText(kOffset)), ReadNumber(InputText(kLimit))};
            auto input = std::istringstream(InputText(kColumns));
            auto field = std::string{};
            while (std::getline(input, field, ',')) {
                const auto colon = field.find(':');
                request.columns.push_back(
                    {field.substr(0, colon), colon == std::string::npos ? 0 : ReadNumber(field.substr(colon + 1))});
            }

            // 表示はその時点のコピー。次の照会まで変化しないことを明示する。
            const auto snapshot = Invoke<PoolSnapshot>("vm.inspect_pool", request);
            if (snapshot.has_value() == false) {
                return;
            }
            const auto start = std::chrono::steady_clock::now();
            auto text = snapshot->pool + "  件数 " + std::to_string(snapshot->length) + " / 容量 " +
                        std::to_string(snapshot->capacity) + "  （取得時点のコピー）\r\n";
            auto widths = std::vector<std::size_t>{};
            const auto append_cell = [&](std::string_view value, std::size_t width) {
                text += value;
                text.append(width - value.size(), ' ');
            };
            append_cell("row", 12);
            for (const auto& column : snapshot->columns) {
                const auto title = column.field + "[" + std::to_string(column.component) + "]:" + column.type;
                auto width = title.size();
                for (const auto& value : column.values) {
                    width = std::max(width, value.size());
                }
                widths.push_back(width + 2);
                append_cell(title, widths.back());
            }
            for (auto row = std::uint32_t{}; row < snapshot->count; ++row) {
                text += "\r\n";
                append_cell(std::to_string(snapshot->offset + row), 12);
                for (auto index = std::size_t{}; index < snapshot->columns.size(); ++index) {
                    append_cell(snapshot->columns[index].values[row], widths[index]);
                }
            }
            const auto wide = Wide(text);
            display_buffer_bytes_ = (wide.capacity() + 1) * sizeof(wchar_t);
            display_format_ns_ = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
            SetText(Control(kSnapshot), wide.c_str());
        }

        void RefreshMetrics()
        {
            if (state_.vm_id.has_value() == false) {
                SetText(Control(kMetricsText), L"VM未読込み。上限を指定して「VMへ読込み」を押してください。");
                return;
            }
            const auto report = Invoke<VmMetricsResult>("vm.get_metrics", VmRequest{*state_.vm_id});
            if (report.has_value() == false) {
                return;
            }

            // 同じruntimeの内訳と、ホストで作る一件の表示コピーを分けて示す。
            auto text = std::string("取得時点の計測 / ") + report->vm.state + "\r\n\r\nVM（bytes）\r\n";
            const auto line = [&](const char* label, std::uint64_t value) {
                text += std::string(label) + ": " + std::to_string(value) + "\r\n";
            };
            line("所有量", report->vm.stats.owned_bytes);
            line("準備時ピーク", report->vm.stats.peak_owned_bytes);
            line("このVMの所有量上限", report->limits.max_owned_bytes);
            line("module", report->memory.module_bytes);
            line("state", report->memory.state_bytes);
            line("集合の予約データ領域", report->memory.pool_data_bytes);
            line("削除ビット", report->memory.pool_removal_bytes);
            line("stackの予約領域", report->memory.stack_bytes);
            line("kernel・準備済み命令", report->memory.kernel_bytes);
            line("管理領域", report->memory.management_bytes);
            line("検証済み最大stack（word）", report->vm.stats.stack_words);
            line("検証済み最大呼出し深さ", report->vm.stats.call_depth);
            line("呼出し深さ上限", report->limits.max_call_depth);
            if (report->last_call.has_value()) {
                text += report->last_call->succeeded ? "\r\n直前の実行：成功\r\n" : "\r\n直前の実行：失敗\r\n";
                text += "関数: " + report->last_call->function + "\r\n";
                line("fuel予算", report->last_call->fuel_budget);
                line("消費fuel", report->last_call->fuel_consumed);
                line("残りfuel", report->last_call->fuel_remaining);
                line("VM実行時間（ns）", report->last_call->elapsed_ns);
            }
            if (report->last_inspection.has_value()) {
                const auto& host = *report->last_inspection;
                text += "\r\n直前の集合取得：ホストの表示用コピー（bytes）\r\n";
                text += "集合: " + host.pool + " / 先頭 " + std::to_string(host.offset) + " / " +
                        std::to_string(host.count) + "行 × " + std::to_string(host.columns) + "列\r\n";
                line("参照したscalar値", host.source_value_bytes);
                line("値のUTF-8文字数（bytes）", host.value_text_bytes);
                line("vectorの予約領域", host.vector_reserved_bytes);
                line("stringの容量上界（小文字列格納を含む）", host.string_capacity_bound_bytes);
                line("取得・文字列変換時間（ns）", host.elapsed_ns);
            }
            if (display_buffer_bytes_ != 0) {
                text += "\r\nこの画面での直前の集合整形\r\n";
                line("UTF-16バッファー容量（bytes）", display_buffer_bytes_);
                line("整形時間（ns）", display_format_ns_);
            }
            text += "\r\nfuelは時間ではありません。未実行の計測は表示しません。\r\n"
                    "VM所有量は予約領域を含みます。OS・アロケーター管理量は対象外です。\r\n"
                    "ホスト値は一件分。JSON・操作履歴・Qt内部・描画費用は含みません。";
            SetText(Control(kMetricsText), Wide(text).c_str());
        }

        void Browse()
        {
            const auto path = QFileDialog::getOpenFileName(this, QString::fromUtf8("プロジェクトを開く"),
                                                           QString::fromStdWString(core_.workspace().wstring()),
                                                           QStringLiteral("Project (*.json)"));
            if (path.isEmpty() == false) {
                SetText(Control(kPath), path.toStdWString().c_str());
            }
        }

        struct PropertyField
        {
            std::string key;
            int component = -1;
            bool read_only = false;
        };
        void BindProperties()
        {
            SetText(Control(kSceneError), L"");
            property_fields_.clear();
            form_revision_ = document_state_.revision;
            if (document_state_.scene.empty()) {
                return;
            }
            const auto json = AsJson(Value(document_state_.scene));
            auto properties = json["settings"];
            if (selected_object_.empty() == false) {
                const auto& objects = json["objects"];
                const auto found = std::find_if(objects.begin(), objects.end(), [&](const auto& o) {
                    return o["id"] == selected_object_;
                });
                if (found == objects.end()) {
                    selected_object_.clear();
                }
                else {
                    properties = *found;
                }
            }
            static const auto kLabels = std::map<std::string, std::wstring>{{"position", L"初期位置"},
                                                                            {"bounds_min", L"移動範囲 最小"},
                                                                            {"bounds_max", L"移動範囲 最大"},
                                                                            {"player_hp", L"プレイヤー HP"},
                                                                            {"enemy_hp", L"敵 HP"},
                                                                            {"move_speed", L"移動速度 / 秒"},
                                                                            {"fire_interval_seconds", L"射撃間隔 / 秒"},
                                                                            {"view_size", L"表示範囲"},
                                                                            {"target", L"注視点"},
                                                                            {"fov_degrees", L"視野角 / 度"},
                                                                            {"visual", L"描画素材"},
                                                                            {"audio", L"音声素材"}};
            for (const auto& [key, value] : properties.items()) {
                if (key == "id" || key == "kind") {
                    continue;
                }
                const auto count = value.is_array() ? int(value.size()) : 1;
                for (auto component = 0; component < count; ++component) {
                    const auto row = int(property_fields_.size());
                    if (row >= 16) {
                        throw std::runtime_error("too many property fields");
                    }
                    const auto index = value.is_array() ? component : -1;
                    property_fields_.push_back({key, index, key == "visual" || key == "audio"});
                    auto label = kLabels.contains(key) ? kLabels.at(key) : Wide(key);
                    if (index >= 0) {
                        label += std::wstring(L" ") + L"XYZ"[index];
                    }
                    SetText(Control(1000 + row * 2), label.c_str());
                    Control(1001 + row * 2)->setAccessibleName(QString::fromStdWString(label));
                    qobject_cast<QLabel*>(Control(1000 + row * 2))->setBuddy(Control(1001 + row * 2));
                    const auto& component_value = index >= 0 ? value[index] : value;
                    auto text = component_value.dump();
                    if (component_value.is_number_float()) {
                        // f32を再現できる最短表記にし、丸め誤差の長い桁を入力欄へ出さない。
                        auto buffer = std::array<char, 64>{};
                        const auto converted =
                            std::to_chars(buffer.data(), buffer.data() + buffer.size(), component_value.get<float>());
                        if (converted.ec == std::errc{}) {
                            text.assign(buffer.data(), converted.ptr);
                        }
                    }
                    SetText(Control(1001 + row * 2), Wide(text).c_str());
                }
            }
            form_dirty_ = false;
        }

        std::optional<std::string> UnsavedChoice()
        {
            if (document_state_.dirty == false) {
                return {};
            }
            const auto choice = QMessageBox::question(
                this, QString::fromUtf8("未保存の変更"), QString::fromUtf8("シーンの変更を保存しますか？"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            return choice == QMessageBox::Save ? "save" : choice == QMessageBox::Discard ? "discard" : "cancel";
        }

        void RefreshScene(bool project_changed)
        {
            if (project_changed) {
                technical_open_ = false;
                ClearItems(kSceneList);
                if (state_.project) {
                    for (const auto& path : state_.project->scenes) {
                        AddItem(kSceneList, Wide(path).c_str());
                    }
                    SelectIndex(kSceneList, int(0));
                }
                if (form_dirty_ == false && document_state_.document_id != core_.scenes().State().document_id) {
                    selected_object_.clear();
                }
            }
            const auto next = core_.scenes().State();
            const auto document_changed = next.revision != document_state_.revision ||
                                          next.document_id != document_state_.document_id ||
                                          next.dirty != document_state_.dirty;
            if (document_changed) {
                document_state_ = next;
                ClearItems(kObjects);
                object_ids_ = {""};
                AddItem(kObjects, L"ゲーム設定");
                if (const auto* scene = core_.scenes().document()) {
                    for (const auto& object : scene->objects) {
                        object_ids_.push_back(object.id);
                        const auto label = Wide(object.kind + " / " + object.id.substr(0, 12));
                        AddItem(kObjects, label.c_str());
                    }
                }
                const auto found = std::find(object_ids_.begin(), object_ids_.end(), selected_object_);
                SelectIndex(kObjects, int(found == object_ids_.end() ? 0 : found - object_ids_.begin()));
                if (form_dirty_ == false) {
                    BindProperties();
                }
            }
            auto status = std::wstring(document_state_.document_id.empty()
                                           ? L"シーンを選んで開いてください"
                                           : Wide(document_state_.path + (document_state_.dirty ? " *" : "") +
                                                  " / revision " + std::to_string(document_state_.revision)));
            if (const auto* scene = core_.scenes().document()) {
                for (const auto* kind : {"player_spawn", "enemy_spawn", "game_camera"}) {
                    if (std::none_of(scene->objects.begin(), scene->objects.end(), [&](const auto& object) {
                            return object.kind == kind;
                        })) {
                        status += L" / 不足: " + Wide(kind);
                    }
                }
            }
            // 位置だけの編集では素材を再デコードせず、参照の組合せが変わったときだけ診断を更新する。
            auto signature = document_state_.document_id;
            if (const auto* scene = core_.scenes().document()) {
                for (const auto& object : scene->objects) {
                    signature += "|" + object.id + "|" + object.visual.value_or("") + "|" + object.audio.value_or("");
                }
            }
            if (signature != asset_reference_signature_) {
                asset_reference_signature_ = signature;
                RefreshAssets();
            }
            SetText(Control(kSceneStatus), status.c_str());
            UpdateAssetControls();
            core_.UpdatePreview();
            if (const auto view = core_.SceneViewHandle(); view && core_.PlayHandle() == nullptr) {
                SetWindowSubclass(view, ViewProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
            }
            SyncViewportControls();
            // 毎tickの表示切替えを避け、入力フォーカスと画面の安定性を保つ。
            if (project_changed || document_changed || core_.SceneViewHandle() != laid_out_view_) {
                Layout();
            }
        }

        bool AllowPlacementInput()
        {
            if (form_dirty_) {
                ShowError("未適用の入力があります。「値を適用」または「値を読み直す」を選んでください。");
                return false;
            }
            return true;
        }

        void SyncViewportControls()
        {
            const auto state = core_.ViewportStatus();
            if (state.transient == false && viewport_button_up_ != 0) {
                viewport_button_up_ = 0;
                if (GetCapture() == core_.SceneViewHandle()) {
                    ReleaseCapture();
                }
            }
            if (state.document_id.empty()) {
                return;
            }
            if (state.selected_id != selected_object_ && form_dirty_ == false) {
                selected_object_ = state.selected_id;
                const auto found = std::find(object_ids_.begin(), object_ids_.end(), selected_object_);
                SelectIndex(kObjects, int(found == object_ids_.end() ? 0 : found - object_ids_.begin()));
                BindProperties();
                Layout();
            }
            const auto candidate = core_.ViewportCandidate();
            if (candidate.has_value() == false && displayed_candidate_ && form_dirty_ == false) {
                BindProperties();
            }
            displayed_candidate_ = candidate.has_value();
            for (auto i = 0u; i < property_fields_.size(); ++i) {
                const auto& field = property_fields_[i];
                Enable(Control(1001 + int(i) * 2),
                       candidate.has_value() == false && EditingEnabled() && field.read_only == false);
                if (candidate && field.key == "position" && field.component >= 0 &&
                    std::size_t(field.component) < candidate->size()) {
                    const auto value = std::to_wstring((*candidate)[field.component]);
                    SetText(Control(1001 + int(i) * 2), value.c_str());
                }
            }
            Enable(Control(kApplyProperties), candidate.has_value() == false && EditingEnabled());
            if (state.transient) {
                SetText(Control(kSceneStatus), candidate ? L"移動中 — マウスを離して確定 / Esc で取消し"
                                                         : L"配置または表示操作中 — Esc で取消し");
            }
        }

        static LRESULT CALLBACK ViewProcedure(HWND view, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
        {
            auto* self = reinterpret_cast<EditorWindow*>(data);
            if (message == WM_NCDESTROY) {
                self->viewport_button_up_ = 0;
                RemoveWindowSubclass(view, ViewProcedure, 1);
                return DefSubclassProc(view, message, wp, lp);
            }
            const auto* scene = self->core_.scenes().document();
            if (scene == nullptr || self->core_.PlayHandle()) {
                return DefSubclassProc(view, message, wp, lp);
            }
            try {
                auto rect = RECT{};
                GetClientRect(view, &rect);
                auto pixel = gengine::Point2d{double(GET_X_LPARAM(lp)), double(GET_Y_LPARAM(lp))};
                if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN ||
                    (message == WM_RBUTTONDOWN && scene->game == "3d")) {
                    // 別ボタンの押下・解放で進行中のドラッグを置き換えない。
                    if (self->viewport_button_up_ != 0) {
                        return 0;
                    }
                    if (self->AllowPlacementInput() == false) {
                        return 0;
                    }
                    SetFocus(view);
                    self->core_.PointerDown(pixel, rect.right, rect.bottom, message == WM_MBUTTONDOWN,
                                            message == WM_RBUTTONDOWN);
                    if (self->core_.ViewportStatus().transient) {
                        self->viewport_button_up_ = message == WM_LBUTTONDOWN   ? WM_LBUTTONUP
                                                    : message == WM_MBUTTONDOWN ? WM_MBUTTONUP
                                                                                : WM_RBUTTONUP;
                        SetCapture(view);
                    }
                    self->Refresh();
                    return 0;
                }
                if (message == WM_MOUSEMOVE && GetCapture() == view) {
                    self->core_.PointerMove(pixel);
                    self->SyncViewportControls();
                    return 0;
                }
                if (message == WM_LBUTTONUP || message == WM_MBUTTONUP ||
                    (message == WM_RBUTTONUP && scene->game == "3d")) {
                    if (message != self->viewport_button_up_) {
                        return 0;
                    }
                    self->viewport_button_up_ = 0;
                    self->core_.PointerUp();
                    if (GetCapture() == view) {
                        ReleaseCapture();
                    }
                    self->Refresh();
                    return 0;
                }
                if (message == WM_MOUSEWHEEL) {
                    auto point = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                    ScreenToClient(view, &point);
                    self->core_.ZoomViewport({double(point.x), double(point.y)}, rect.right, rect.bottom,
                                             std::pow(1.2, -double(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA));
                    self->SyncViewportControls();
                    return 0;
                }
                if (message == WM_KEYDOWN && wp == VK_DELETE && self->AllowPlacementInput()) {
                    self->core_.DeleteViewportObject();
                    self->Refresh();
                    return 0;
                }
                if (message == WM_SIZE || message == WM_KILLFOCUS || message == WM_CAPTURECHANGED ||
                    (message == WM_KEYDOWN && wp == VK_ESCAPE)) {
                    self->viewport_button_up_ = 0;
                    self->core_.CancelViewportGesture();
                    if (GetCapture() == view) {
                        ReleaseCapture();
                    }
                    self->SyncViewportControls();
                    if (message == WM_KEYDOWN) {
                        return 0;
                    }
                }
            } catch (const std::exception& error) {
                self->viewport_button_up_ = 0;
                self->core_.CancelViewportGesture();
                if (GetCapture() == view) {
                    ReleaseCapture();
                }
                self->ShowError(error.what());
            }
            return DefSubclassProc(view, message, wp, lp);
        }

        bool SceneCommand(int id)
        {
            if ((id == kAddObject || id == kDeleteObject || id == kSceneOpen || id == kReloadScene ||
                 id == kSaveScene || id == kUndo || id == kRedo || id == kRecovery) &&
                AllowPlacementInput() == false) {
                return true;
            }
            const auto doc = document_state_.document_id;
            const auto revision = document_state_.revision;
            if (id == kFitView) {
                Invoke<ViewportState>("viewport.reset_view", DocumentRequest{doc, revision}, true);
            }
            else if (id == kSceneOpen && state_.project) {
                const auto index = Index(kSceneList);
                if (index >= 0 && std::size_t(index) < state_.project->scenes.size()) {
                    Invoke<DocumentInfo>("document.open",
                                         DocumentOpen{state_.project->scenes[index], revision, UnsavedChoice()}, true);
                }
            }
            else if (id == kSaveScene) {
                Invoke<DocumentInfo>("document.save", DocumentRequest{doc, revision}, true);
            }
            else if (id == kReloadScene) {
                Invoke<DocumentInfo>("document.reload", DocumentReload{doc, revision, UnsavedChoice()}, true);
            }
            else if (id == kUndo || id == kRedo) {
                const auto history = id == kUndo ? document_state_.undo_id : document_state_.redo_id;
                if (history) {
                    Invoke<DocumentInfo>(id == kUndo ? "history.undo" : "history.redo",
                                         HistoryRequest{doc, revision, *history}, true);
                }
            }
            else if (id == kAddObject) {
                const auto index = Index(kObjectKind);
                const auto kinds = std::array{"player_spawn", "enemy_spawn", "game_camera"};
                if (index >= 0 && index < 3) {
                    if (core_.scenes().document() && index < 2) {
                        core_.BeginPlacement(kinds[index]);
                        SetFocus(core_.SceneViewHandle());
                    }
                    else {
                        Invoke<DocumentInfo>("scene.create_object", SceneCreate{doc, revision, kinds[index], {}}, true);
                    }
                }
            }
            else if (id == kDeleteObject && selected_object_.empty() == false) {
                Invoke<DocumentInfo>("scene.delete_objects", SceneDelete{doc, revision, {selected_object_}}, true);
            }
            else if (id == kPropertyRefresh) {
                form_dirty_ = false;
                BindProperties();
            }
            else if (id == kApplyProperties) {
                auto properties = Json::object();
                for (auto i = 0u; i < property_fields_.size(); ++i) {
                    const auto& field = property_fields_[i];
                    if (field.read_only) {
                        continue;
                    }
                    const auto text = InputText(1001 + int(i) * 2);
                    Json value;
                    try {
                        value = ParseJson(text, 128);
                    } catch (...) {
                        throw std::invalid_argument(field.key + ": enter a number");
                    }
                    if (value.is_number() == false) {
                        throw std::invalid_argument(field.key + ": enter a number");
                    }
                    if (field.component < 0) {
                        properties[field.key] = value;
                    }
                    else {
                        if (properties.contains(field.key) == false) {
                            properties[field.key] = Json::array();
                        }
                        properties[field.key].push_back(value);
                    }
                }
                const auto values = std::get<Value::Object>(FromJson(properties).data);
                const auto applied =
                    selected_object_.empty()
                        ? Invoke<DocumentInfo>("scene.set_settings", SceneSettingsRequest{doc, form_revision_, values},
                                               true)
                        : Invoke<DocumentInfo>("scene.set_properties",
                                               SceneSet{doc, form_revision_, selected_object_, values}, true);
                if (applied) {
                    form_dirty_ = false;
                }
            }
            else if (id == kRecovery) {
                if (core_.scenes().State().recovery_available == false) {
                    QMessageBox::information(this, QString::fromUtf8("復旧"),
                                             QString::fromUtf8("復旧入力はありません。"));
                }
                else {
                    const auto choice = QMessageBox::question(
                        this, QString::fromUtf8("復旧"),
                        QString::fromUtf8("復旧入力を読み込みますか？\nYes: 復元 / No: 破棄 / Cancel: 中止"),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Cancel);
                    if (choice != QMessageBox::Cancel) {
                        Invoke<DocumentInfo>(
                            "document.recover",
                            RecoveryRequest{doc, revision, choice == QMessageBox::Yes ? "restore" : "discard"}, true);
                    }
                }
            }
            else if (id == kSceneDiagnostics) {
                QMessageBox::information(this, QString::fromUtf8("診断"),
                                         QString::fromStdString(InputText(kDiagnostics)));
            }
            else {
                return false;
            }
            Refresh();
            return true;
        }

        void UpdateAssetControls()
        {
            const auto* scene = core_.scenes().document();
            const gengine::SceneObject* target = nullptr;
            if (scene) {
                for (const auto& object : scene->objects) {
                    if (object.id == selected_object_ && object.kind != "game_camera") {
                        target = &object;
                    }
                }
            }
            const auto ready = target && EditingEnabled() && form_dirty_ == false;
            Enable(Control(kAssetAssign), ready);
            Enable(Control(kAssetClear), ready);
            Enable(Control(kAssetSlot), ready);
            SetText(Control(kAssetTarget),
                    target ? Wide("割当て先: " + target->id +
                                  (target->kind == "player_spawn" ? " / 音声は射撃音" : " / 音声は敵弾の命中音"))
                                 .c_str()
                           : L"シーンでプレイヤーまたは敵を選んでください");
        }

        void RefreshAssets()
        {
            if (state_.project) {
                Invoke<AssetCatalog>("asset.list", EmptyRequest{});
            }
            SyncAssets();
        }

        void SyncAssets()
        {
            const auto previous = Index(kAssetList);
            const auto selected = previous >= 0 && std::size_t(previous) < asset_catalog_.entries.size()
                                      ? asset_catalog_.entries[previous].path
                                      : std::string{};
            asset_catalog_ = state_.project ? core_.asset_catalog() : AssetCatalog{};
            displayed_asset_revision_ = core_.asset_catalog_revision();
            ClearItems(kAssetList);
            SetText(Control(kAssetDetail), L"");
            for (const auto& entry : asset_catalog_.entries) {
                const auto label = entry.path + "  |  " +
                                   (entry.diagnostic.empty() ? entry.summary : "参照エラー: " + entry.diagnostic);
                AddItem(kAssetList, Wide(label).c_str());
            }
            for (auto row = std::size_t{0}; row < asset_catalog_.entries.size(); ++row) {
                if (asset_catalog_.entries[row].path == selected) {
                    SelectIndex(kAssetList, static_cast<int>(row));
                    break;
                }
            }
            auto issues = std::string{};
            for (const auto& reference : asset_catalog_.references) {
                if (reference.diagnostic.empty() == false) {
                    issues += reference.object_id + "." + reference.slot + ": " + reference.path + "\n" +
                              reference.diagnostic + "\n";
                }
            }
            SetText(Control(kAssetIssues), Wide(issues).c_str());
            Control(kAssetIssues)->setVisible(issues.empty() == false);
            if (issues.empty() == false) {
                SetText(Control(kAssetDetail), Wide(issues).c_str());
            }
            UpdateAssetControls();
        }

        bool AssetCommand(int id)
        {
            if (id == kAssets) {
                if (AllowPlacementInput() == false) {
                    return true;
                }
                core_.CancelViewportGesture();
                assets_open_ = true;
                technical_open_ = false;
                RefreshAssets();
                Layout();
                return true;
            }
            if (id == kAssetAssign || id == kAssetClear) {
                if (AllowPlacementInput() == false || selected_object_.empty()) {
                    return true;
                }
                auto value = Value{};
                if (id == kAssetAssign) {
                    const auto row = Index(kAssetList);
                    if (row < 0 || std::size_t(row) >= asset_catalog_.entries.size()) {
                        ShowError("割り当てる素材を一覧から選んでください。");
                        return true;
                    }
                    value = asset_catalog_.entries[row].path;
                }
                const auto slot = Index(kAssetSlot) == 0 ? "visual" : "audio";
                Invoke<DocumentInfo>(
                    "scene.set_properties",
                    SceneSet{document_state_.document_id, document_state_.revision, selected_object_, {{slot, value}}},
                    true);
                Refresh();
                RefreshAssets();
                return true;
            }
            if (id == kAssetRefresh) {
                RefreshAssets();
                return true;
            }
            if ((id == kAssetRegister || id == kAssetBrowse) && state_.project) {
                if (id == kAssetBrowse) {
                    const auto root = (core_.workspace() / Wide(state_.project->path)).parent_path();
                    const auto selected = QFileDialog::getOpenFileName(
                        this, QString::fromUtf8("プロジェクト内の素材を登録"), QString::fromStdWString(root.wstring()),
                        QStringLiteral("Assets (*.png *.gmesh *.wav)"));
                    if (selected.isEmpty() == false) {
                        const auto relative = std::filesystem::path(selected.toStdWString()).lexically_relative(root);
                        SetText(Control(kAssetPath), relative.generic_wstring().c_str());
                    }
                }
                else {
                    Invoke<EditorState>(
                        "asset.register",
                        AssetRegister{state_.project->project_id, state_.project->revision, InputText(kAssetPath)},
                        true);
                    Refresh();
                    RefreshAssets();
                }
                return true;
            }
            return false;
        }

        void Command(int id)
        {
            if (AssetCommand(id)) {
                return;
            }
            if ((id == kClose || id == kPlay || id == kBuild) && AllowPlacementInput() == false) {
                return;
            }
            if (id == kTechnical) {
                assets_open_ = false;
                technical_open_ = technical_open_ == false;
                Layout();
                return;
            }
            if (SceneCommand(id)) {
                return;
            }
            // 表示の開閉はUIだけの操作。閉じたときは表示用コピーも消す。
            if (id == kInspector || id == kMetrics) {
                if (id == kInspector) {
                    inspector_open_ = inspector_open_ == false;
                    metrics_open_ = false;
                }
                else {
                    metrics_open_ = metrics_open_ == false;
                    inspector_open_ = false;
                }
                SetText(Control(kInspector), inspector_open_ ? L"集合表示を閉じる" : L"集合を表示");
                SetText(Control(kMetrics), metrics_open_ ? L"計測を閉じる" : L"計測・予算");
                SetText(Control(kSnapshot), L"");
                Layout();
                if (inspector_open_) {
                    Inspect();
                }
                if (metrics_open_) {
                    RefreshMetrics();
                }
                return;
            }
            if (id == kRefreshMetrics) {
                RefreshMetrics();
                return;
            }
            if (id == kInspect) {
                Inspect();
                return;
            }
            if (id == kLoadVm || id == kUnloadVm || id == kReset || id == kCall) {
                SetText(Control(kSnapshot), L"VMを操作しました。「表示を更新」で再取得してください。");
            }
            if (id == kPlayOptions) {
                play_options_open_ = play_options_open_ == false;
                Layout();
                return;
            }
            if (id == kBuild || (id == kPlay && Index(kPlayMode) == 1)) {
                if (state_.project && core_.scenes().document()) {
                    auto choice = std::optional<std::string>{};
                    if (document_state_.dirty) {
                        const auto answer =
                            QMessageBox::question(this, QString::fromUtf8("ビルド前に保存"),
                                                  QString::fromUtf8("未保存のシーンを保存してビルドしますか？"),
                                                  QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
                        if (answer != QMessageBox::Save) {
                            return;
                        }
                        choice = "save";
                    }
                    const auto index = Index(kSources);
                    if (index < 0 || std::size_t(index) >= state_.project->sources.size()) {
                        return;
                    }
                    const auto accepted =
                        Invoke<CompileAccepted>("build.start",
                                                BuildStart{state_.project->project_id, state_.project->revision,
                                                           state_.project->sources[std::size_t(index)],
                                                           document_state_.path, choice, document_state_.revision},
                                                true);
                    if (accepted && accepted->status == "Accepted" && id == kPlay) {
                        pending_launch_ = accepted->job_id;
                    }
                    Refresh();
                }
                return;
            }
            if (id == kStopPlay && core_.RunnerActive()) {
                Invoke<RunnerState>("standalone.stop", RunnerRequest{core_.RunnerStatus().run_id}, true);
                Refresh();
                return;
            }
            if (id == kPlay && state_.last_job.has_value()) {
                Invoke<PlayState>(
                    "play.start",
                    PlayStart{
                        *state_.last_job, static_cast<std::uint32_t>(Index(kPreset)),
                        core_.scenes().document() ? core_.scenes().document()->game
                        : Index(kGameMode) == 1   ? "3d"
                                                  : "2d",
                        document_state_.document_id.empty() ? std::nullopt : std::optional(document_state_.document_id),
                        document_state_.document_id.empty() ? std::nullopt : std::optional(document_state_.revision)},
                    true);
                Layout();
                return;
            }
            if (id == kPausePlay || id == kStopPlay || id == kResetPlay || id == kNormalPlay) {
                const auto status = core_.PlayStatus();
                auto request = PlayRequest{status.play_id, {}, {}};
                if (id == kPausePlay) {
                    request.paused = status.paused == false;
                }
                if (id == kNormalPlay) {
                    request.normal = true;
                }
                Invoke<PlayState>(id == kPausePlay  ? "play.pause"
                                  : id == kStopPlay ? "play.stop"
                                                    : "play.reset",
                                  request, true);
                Layout();
                return;
            }
            auto vm_changed = false;
            if (id == kLoadVm && state_.last_job.has_value()) {
                vm_changed = Invoke<EditorState>(
                                 "vm.load", VmLoadRequest{*state_.last_job, ReadNumber(InputText(kOwnedLimit))}, true)
                                 .has_value();
            }
            if (state_.vm_id.has_value()) {
                if (id == kUnloadVm) {
                    vm_changed = Invoke<EditorState>("vm.unload", VmRequest{*state_.vm_id}, true).has_value();
                }
                else if (id == kReset) {
                    vm_changed = Invoke<ResetResult>("vm.reset", VmRequest{*state_.vm_id}, true).has_value();
                }
                else if (id == kCall) {
                    vm_changed =
                        Invoke<ResetResult>(
                            "vm.call", VmCallRequest{*state_.vm_id, InputText(kFunction), ReadNumber(InputText(kFuel))},
                            true)
                            .has_value();
                }
            }

            if (vm_changed) {
                // 復帰に成功した後は、一時的な操作エラーを残さず通常の診断表示へ戻す。
                displayed_job_.clear();
                if (id != kCall) {
                    display_buffer_bytes_ = 0;
                    display_format_ns_ = 0;
                }
            }

            // 作成・読込みはパス欄の入力を使い、選択ダイアログも同じ操作へ接続する。
            if (id == kCreate || id == kOpen) {
                if (id == kCreate) {
                    const auto selected = Index(kTemplate);
                    Invoke<EditorState>("project.create",
                                        ProjectCreateRequest{InputText(), selected == 1   ? "3d"
                                                                          : selected == 2 ? "vm"
                                                                                          : "2d"},
                                        true);
                }
                else {
                    Invoke<EditorState>("project.open", ProjectPathRequest{InputText()}, true);
                }
                Refresh();
                return;
            }

            if (id == kBrowse) {
                Browse();
                return;
            }

            // 開いているプロジェクトや実行中ジョブのIDを使って操作する。
            if (id == kClose && state_.project.has_value()) {
                Invoke<EditorState>("project.close",
                                    CloseRequest{state_.project->project_id, state_.project->revision, UnsavedChoice(),
                                                 document_state_.revision},
                                    true);
            }
            else if (id == kCancel && state_.active_job.has_value()) {
                Invoke<JobInfo>("job.cancel", JobRequest{*state_.active_job}, true);
            }
            else if ((id == kCompile || id == kEdit) && state_.project.has_value()) {
                // 一覧の選択を確認してから、対象のソースをコンパイルまたは外部編集する。
                const auto index = Index(kSources);
                if (index == LB_ERR || static_cast<std::size_t>(index) >= state_.project->sources.size()) {
                    ShowError("ソースファイルを選択してください。");
                    return;
                }

                const auto& project = *state_.project;
                const auto& source = project.sources[static_cast<std::size_t>(index)];
                if (id == kCompile) {
                    Invoke<CompileAccepted>("compile.start",
                                            CompileRequest{project.project_id, project.revision, source}, true);
                }
                else {
                    // 編集は外部へ委ねる。次のコンパイルはディスクへ保存された内容を使う。
                    const auto path = core_.workspace() / std::filesystem::path(Wide(project.path)).parent_path() /
                                      std::filesystem::path(Wide(source));
                    const auto result = ShellExecuteW(window_, L"open", L"notepad.exe",
                                                      QuoteArgument(path.native()).c_str(), nullptr, SW_SHOW);
                    if (reinterpret_cast<INT_PTR>(result) <= 32) {
                        ShowError("メモ帳を起動できませんでした。外部エディターでソースを開いてくだ"
                                  "さい。");
                    }
                }
            }

            Refresh();
            // 失敗理由を消さない。失敗後の統計は「計測を更新」で取得できる。
            if (vm_changed && metrics_open_) {
                RefreshMetrics();
            }
        }

        void Refresh()
        {
            // MCPからの変更も拾えるように、画面自身の操作履歴ではなく共通状態を読む。
            const auto current = Invoke<EditorState>("editor.get_state", EmptyRequest{});
            if (current.has_value() == false) {
                return;
            }

            // プロジェクトが切り替わったときだけ一覧を作り直し、通常の選択状態を保つ。
            const bool project_changed =
                current->project.has_value() != state_.project.has_value() ||
                (current->project.has_value() &&
                 (state_.project.has_value() == false || current->project->revision != state_.project->revision));
            if (state_.vm_id != current->vm_id) {
                SetText(Control(kSnapshot), L"VMが切り替わりました。「表示を更新」で取得してください。");
                SetText(Control(kMetricsText), L"VMが切り替わりました。「計測を更新」で取得してください。");
                display_buffer_bytes_ = 0;
                display_format_ns_ = 0;
            }
            const auto job_changed = state_.active_job != current->active_job;
            state_ = *current;
            // 実行ボタンで受け付けた同じジョブだけを起動する。失敗・取消しなら予約を解除する。
            if (pending_launch_ && state_.active_job.has_value() == false) {
                const auto id = std::exchange(pending_launch_, std::nullopt);
                const auto job = Invoke<JobInfo>("job.get", JobRequest{*id});
                if (job && job->state == "Succeeded") {
                    Invoke<RunnerState>("standalone.start", JobRequest{*id}, true);
                }
            }
            const auto& runner = core_.RunnerStatus();
            // 非同期の受付・完了・子の起動後も、停止ボタンと編集可否を同じ状態へ揃える。
            if (job_changed || displayed_runner_state_ != runner.state) {
                displayed_runner_state_ = runner.state;
                Layout();
            }
            Control(kRunnerStatus)->setVisible(runner.run_id.empty() == false);
            SetText(Control(kRunnerStatus),
                    Wide("GameRunner: " + runner.state +
                         (runner.exit_code ? " / exit " + std::to_string(*runner.exit_code) : "") +
                         (runner.diagnostic.empty() ? "" : " / " + runner.diagnostic))
                        .c_str());
            if (project_changed) {
                // 読込みに成功して作業場所が変わったとき、表示も共通コアの基準へ揃える。
                const auto workspace =
                    L"作業フォルダー: " + core_.workspace().wstring() + (mcp_ ? L"   /   MCP 接続" : L"");
                SetText(Control(kWorkspace), workspace.c_str());
                ClearItems(kSources);
                if (state_.project.has_value()) {
                    for (const auto& source : state_.project->sources) {
                        AddItem(kSources, Wide(source).c_str());
                    }
                    SelectIndex(kSources, int(0));
                    SetText(Control(kPath), Wide(state_.project->path).c_str());
                    SetText(Control(kProject), Wide(state_.project->path).c_str());
                }
                else {
                    SetText(Control(kProject), L"プロジェクトを作成するか、開いてください");
                }
                SetText(Control(kDiagnostics), L"");
                SetText(Control(kStatus), L"準備完了");
                displayed_job_.clear();
            }

            if (core_.asset_catalog_revision() != displayed_asset_revision_) {
                SyncAssets();
            }
            RefreshScene(project_changed);
            if (project_changed) {
                RefreshAssets();
            }
            for (const auto id : {kAssets, kAssetRefresh, kAssetBrowse, kAssetRegister, kAssetPath}) {
                Enable(Control(id), state_.project.has_value() && state_.active_job.has_value() == false &&
                                        core_.PlayHandle() == nullptr);
            }

            // 実行中のジョブがある間は切替えと再コンパイルを止め、取消しを有効にする。
            const bool open = state_.project.has_value();
            const bool busy = state_.active_job.has_value();
            for (const int id : {kCreate, kOpen, kBrowse, kPath}) {
                Enable(Control(id), open == false && busy == false);
            }
            Enable(Control(kClose), open && busy == false);
            Enable(Control(kCompile), open && busy == false);
            Enable(Control(kEdit), open);
            Enable(Control(kCancel), busy);

            // VMの状態と診断は集合表示を閉じても確認できる。
            const auto loaded = state_.vm_id.has_value();
            for (const auto id : {kUnloadVm, kReset, kInspect, kFunction}) {
                Enable(Control(id), loaded);
            }
            // VMを解放した後も、開いている表示は閉じられる。
            Enable(Control(kInspector), loaded || inspector_open_);
            Enable(Control(kCall), false);
            auto vm_text = std::string("VM未読込み");
            if (loaded) {
                const auto vm_state = Invoke<VmStateResult>("vm.get_state", VmRequest{*state_.vm_id});
                const auto diagnostic = Invoke<VmDiagnosticResult>("vm.get_diagnostics", VmRequest{*state_.vm_id});
                if (vm_state.has_value()) {
                    vm_text = "VM: " + vm_state->state + "  / 読込みジョブ: " + *state_.loaded_job;
                    Enable(Control(kCall), vm_state->state == "Ready");
                }
                if (diagnostic.has_value() && diagnostic->present) {
                    vm_text += "\r\n" + diagnostic->code + "  " + diagnostic->source;
                    if (diagnostic->line.has_value()) {
                        vm_text += ":" + std::to_string(*diagnostic->line);
                    }
                    vm_text += "  " + diagnostic->function;
                }
            }
            SetText(Control(kVmStatus), Wide(vm_text).c_str());
            const auto can_load =
                state_.last_job.has_value() &&
                core_.dispatcher().Availability("vm.load", 1, Encode(JobRequest{*state_.last_job})).has_value() ==
                    false;
            Enable(Control(kLoadVm), can_load);

            // 開いている版の結果だけを表示し、前のプロジェクトの診断を混ぜない。
            if (state_.last_job.has_value() && open) {
                const auto job = Invoke<JobInfo>("job.get", JobRequest{*state_.last_job});
                if (job.has_value() && job->project_id == state_.project->project_id &&
                    job->input_revision == state_.project->revision) {
                    const auto rendered = ToJson(Encode(*job));
                    if (rendered != displayed_job_) {
                        // 結果が変わったときだけ更新し、診断を読んでいる最中の再描画を減らす。
                        displayed_job_ = rendered;
                        const auto status = job->state == "Queued" ? L"受付済み"
                                            : job->state == "Running"
                                                ? (job->cancel_requested ? L"取消し処理中" : L"コンパイル中")
                                            : job->state == "Succeeded" ? L"コンパイル成功"
                                            : job->state == "Cancelled" ? L"取消し完了"
                                                                        : L"コンパイル失敗";
                        SetText(Control(kStatus), job->kind == "Build"
                                                      ? Wide("ビルド: " + job->state + " / " + job->phase).c_str()
                                                      : status);

                        auto text = std::string{};
                        for (const auto& diagnostic : job->diagnostics) {
                            text += diagnostic.source;
                            if (diagnostic.line.has_value()) {
                                text +=
                                    ":" + std::to_string(*diagnostic.line) + ":" + std::to_string(*diagnostic.column);
                            }
                            text += "\r\n" + diagnostic.message + "\r\n\r\n";
                        }

                        if (job->artifact.has_value()) {
                            text += "成果物: " + *job->artifact + "\r\n\r\n";
                        }
                        text += "入力: " + job->source;
                        SetText(Control(kDiagnostics), Wide(text).c_str());
                    }
                }
            }
        }

        EditorCore core_;
        bool displayed_candidate_ = false;
        UINT viewport_button_up_ = 0;
        McpAdapter adapter_;
        bool mcp_ = false;
        bool automatic_workspace_ = false;
        std::unique_ptr<StdioTransport> transport_;

        HWND window_ = nullptr;
        HWND laid_out_play_ = nullptr;
        HWND laid_out_view_ = nullptr;
        bool play_options_open_ = false;
        std::optional<std::string> pending_launch_;
        std::string displayed_runner_state_;
        bool shutting_down_ = false;

        // 前回の表示内容を保持し、必要な箇所だけを再描画する。
        bool inspector_open_ = false;
        bool metrics_open_ = false;
        std::uint64_t display_buffer_bytes_ = 0;
        std::uint64_t display_format_ns_ = 0;
        EditorState state_;
        DocumentInfo document_state_;
        bool assets_open_ = false;
        AssetCatalog asset_catalog_;
        std::string asset_reference_signature_;
        std::uint64_t displayed_asset_revision_ = 0;
        std::vector<PropertyField> property_fields_;
        std::vector<std::string> object_ids_;
        std::string selected_object_;
        std::uint64_t form_revision_ = 0;
        bool form_dirty_ = false;
        bool technical_open_ = false;
        std::string displayed_job_;
        std::uint64_t sequence_ = 0;
        std::uint64_t ticks_ = 0;

        std::map<int, QWidget*> controls_;
        QStackedWidget* pages_ = nullptr;
        QWidget* view_host_ = nullptr;
        QWidget* inspector_fields_ = nullptr;
        QTimer timer_;
    };
} // namespace

#ifndef GENGINE_EDITOR_TEST
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // Qtの初期化前に起動条件を検査する。MCPの配置不足をダイアログ待ちにしない。
    auto app = std::unique_ptr<QApplication>{};
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    bool mcp = false;
    try {
        // Editorと同じ場所のコンパイラーを使い、作業フォルダーの既定値を決める。
        auto executable = std::array<wchar_t, 32768>{};
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        const auto compiler = std::filesystem::path(executable.data()).parent_path() / L"glangc.exe";
        auto workspace = std::filesystem::current_path();

        // 引数の領域は例外時にも解放する。MCP起動では作業フォルダーの明示を必須にする。
        bool workspace_set = false;
        int count = 0;
        auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (arguments == nullptr) {
            throw std::runtime_error("cannot read command line");
        }

        struct ArgumentsCleanup
        {
            wchar_t** value;
            ~ArgumentsCleanup()
            {
                LocalFree(value);
            }
        } cleanup{arguments};

        auto arguments_valid = true;
        for (int index = 1; index < count; ++index) {
            const auto argument = std::wstring_view(arguments[index]);
            if (argument == L"--mcp") {
                mcp = true;
            }
            else if (argument == L"--workspace" && index + 1 < count) {
                workspace = arguments[++index];
                workspace_set = true;
            }
            else {
                arguments_valid = false;
            }
        }

        // 不正な引数が先にあっても、後続のMCP指定を確かめてから診断の出力先を決める。
        if (arguments_valid == false) {
            throw std::invalid_argument("usage: gengine_editor.exe [--mcp] [--workspace DIRECTORY]");
        }
        if (mcp && workspace_set == false) {
            throw std::invalid_argument("--mcp requires an explicit --workspace DIRECTORY");
        }
        if (std::filesystem::is_regular_file(compiler.parent_path() / "platforms/qwindows.dll") == false) {
            throw std::runtime_error("Qt platform plugin is missing: restore Editor/platforms/qwindows.dll");
        }

        // アプリケーションの生存期間に、共通コアと通信・画面の生存期間を揃える。
        app = std::make_unique<QApplication>(__argc, __argv);
        auto application = EditorWindow(workspace, compiler, mcp, mcp == false && workspace_set == false);
        result = application.Run();
    } catch (const std::exception& error) {
        const auto text = std::string(error.what()) + "\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);

        // MCPの起動失敗をモーダルで待たせない。通常起動だけ画面に理由を示す。
        if (mcp == false) {
            if (app) {
                QMessageBox::critical(nullptr, QStringLiteral("gEngine Editor"), QString::fromStdString(text));
            }
            else {
                MessageBoxW(nullptr, Wide(text).c_str(), L"gEngine Editor", MB_OK | MB_ICONERROR);
            }
        }
    }

    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return result;
}
#endif
