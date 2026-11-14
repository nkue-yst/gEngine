// 本体と同じQt画面を使い、公開操作を入力イベントと共通Dispatcherから交互に実行する。
#define GENGINE_EDITOR_TEST
#include "../apps/Editor/main.cpp"
#include <QElapsedTimer>
#include <QImage>
#include <QInputMethodEvent>
#include <QScreen>
#include <QTest>
#include <iostream>
#include <psapi.h>

namespace
{
    void Require(bool condition, const char* reason)
    {
        if (condition == false) {
            throw std::runtime_error(reason);
        }
    }

    class QtEditorTest
    {
      public:
        static void Run(EditorWindow& window, const std::filesystem::path& output)
        {
            auto mcp = McpAdapter(window.core_);
            mcp.Receive(
                {R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"Qt parity","version":"1"}}})",
                 R"({"jsonrpc":"2.0","method":"notifications/initialized"})"});
            const auto click = [&](int id) {
                QTest::mouseClick(window.Control(id), Qt::LeftButton);
                QCoreApplication::processEvents();
            };
            const auto edit = [&](int id, const QString& text) {
                auto* field = qobject_cast<QLineEdit*>(window.Control(id));
                Require(field != nullptr, "missing input");
                field->setFocus();
                field->selectAll();
                QTest::keyClicks(field, text);
            };
            // IMEの変換中文字は確定前に保存されず、確定文字だけが入力へ入る。
            auto* path = qobject_cast<QLineEdit*>(window.Control(kPath));
            path->clear();
            path->setFocus();
            auto composition = QInputMethodEvent(QString::fromUtf8("にほんご"), {});
            QCoreApplication::sendEvent(path, &composition);
            Require(path->text().isEmpty(), "IME preedit committed early");
            auto committed = QInputMethodEvent{};
            committed.setCommitString(QString::fromUtf8("日本語 空白"));
            QCoreApplication::sendEvent(path, &committed);
            Require(path->text() == QString::fromUtf8("日本語 空白"), "IME commit lost");

            for (const auto* game : {"2d", "3d"}) {
                path->setText(QString::fromUtf8("日本語 空白 ") + QString::fromStdString(NewId()));
                window.SelectIndex(kTemplate, std::string_view(game) == "2d" ? 0 : 1);
                click(kCreate);
                Require(window.state_.project.has_value(), "GUI project creation failed");
                // 素材の登録は原本の論理パスを保存し、GUIも共通検証の結果を表示する。
                const auto project_root = (window.core_.workspace() / Wide(window.state_.project->path)).parent_path();
                const auto asset_name = std::string_view(game) == "2d" ? "sprite.png" : "sphere.gmesh";
                auto exe = std::array<wchar_t, 32768>{};
                GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
                const auto original = ReadFile(
                    std::filesystem::path(exe.data()).parent_path() / "content/assets" / asset_name, 4 * 1024 * 1024);
                WriteNewFile(project_root / asset_name, original);
                click(kAssets);
                Require(window.pages_->currentIndex() == 2, "asset page did not open");
                edit(kAssetPath, QString::fromUtf8(asset_name));
                click(kAssetRegister);
                Require(window.state_.project->assets.size() == 1 && window.asset_catalog_.entries.size() == 1 &&
                            window.asset_catalog_.entries.front().diagnostic.empty(),
                        "GUI registration failed");
                Require(ReadFile(project_root / asset_name, 4 * 1024 * 1024) == original,
                        "registration changed original");
                Require(window.grab().save(
                            QString::fromStdWString((output / (std::string("assets-") + game + ".png")).wstring())),
                        "asset page screenshot failed");
                window.assets_open_ = false;
                window.Layout();
                click(kSceneOpen);
                Require(window.core_.scenes().document() != nullptr, "GUI scene open failed");
                auto* list = qobject_cast<QListWidget*>(window.Control(kObjects));
                Require(list->count() == 4, "object list missing entries");
                list->setCurrentRow(1);
                click(kAssets);
                window.SelectIndex(kAssetList, 0);
                window.SelectIndex(kAssetSlot, 0);
                click(kAssetAssign);
                Require(window.core_.scenes().document()->objects.front().visual == asset_name,
                        "GUI assignment failed");
                click(kAssetClear);
                Require(window.core_.scenes().document()->objects.front().visual.has_value() == false,
                        "GUI reference clear failed");
                click(kUndo);
                Require(window.core_.scenes().document()->objects.front().visual == asset_name,
                        "GUI reference undo failed");
                Require(window.grab().save(
                            QString::fromStdWString((output / (std::string("assigned-") + game + ".png")).wstring())),
                        "assignment page screenshot failed");
                // MCP等が共通操作を実行した後も、GUIは再デコードせず同じ診断を受け取る。
                WriteAtomicFile(project_root / asset_name, "broken");
                const auto inspected = window.core_.dispatcher().SubmitTyped("asset.list", EmptyRequest{});
                Require(inspected.has_value(), "external asset query rejected");
                window.core_.dispatcher().Pump();
                window.Refresh();
                Require(window.asset_catalog_.references.size() == 1 &&
                            window.asset_catalog_.references.front().diagnostic.empty() == false &&
                            window.Control(kAssetIssues)->isHidden() == false,
                        "GUI missed shared reference diagnostic");
                // ラベルの変更で発生したQtのレイアウト要求を処理してから、実際の表示領域を照合する。
                QCoreApplication::processEvents();
                window.centralWidget()->layout()->activate();
                auto* detail = qobject_cast<QLabel*>(window.Control(kAssetDetail));
                Require(detail->height() >= detail->heightForWidth(detail->width()), "reference diagnostic is clipped");
                Require(window.grab().save(
                            QString::fromStdWString((output / (std::string("broken-") + game + ".png")).wstring())),
                        "broken reference page screenshot failed");
                WriteAtomicFile(project_root / asset_name, original);
                click(kAssetRefresh);
                Require(window.asset_catalog_.references.front().diagnostic.empty(),
                        "GUI repair did not clear diagnostic");
                click(kAssetClear);
                window.assets_open_ = false;
                window.Layout();
                list->setCurrentRow(0);

                if (std::string_view(game) == "2d") {
                    // 素材一覧相当の5000件を既存の一覧部品に載せ、表示コピーの費用を測る。
                    auto* sources = qobject_cast<QListWidget*>(window.Control(kSources));
                    auto rows = QStringList{};
                    for (auto i = 0; i < 5000; ++i) {
                        rows.push_back(QStringLiteral("Content/items/object_%1.asset").arg(i));
                    }
                    auto memory = PROCESS_MEMORY_COUNTERS_EX{};
                    memory.cb = sizeof(memory);
                    Require(GetProcessMemoryInfo(GetCurrentProcess(),
                                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                                 sizeof(memory)) != FALSE,
                            "memory query failed");
                    const auto before_list = memory.PrivateUsage;
                    auto elapsed = QElapsedTimer{};
                    elapsed.start();
                    sources->setUpdatesEnabled(false);
                    sources->clear();
                    sources->addItems(rows);
                    sources->setUpdatesEnabled(true);
                    QCoreApplication::processEvents();
                    const auto list_ms = double(elapsed.nsecsElapsed()) / 1000000;
                    Require(GetProcessMemoryInfo(GetCurrentProcess(),
                                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                                 sizeof(memory)) != FALSE,
                            "memory query failed");
                    Require(memory.PrivateUsage < 512ull * 1024 * 1024 &&
                                memory.PrivateUsage - before_list < 128ull * 1024 * 1024,
                            "list exceeds memory budget");
                    std::cout << "5000 rows: milliseconds=" << list_ms << " private_bytes=" << memory.PrivateUsage
                              << " delta=" << memory.PrivateUsage - before_list << '\n';
                    sources->clear();
                    sources->addItem(QStringLiteral("scripts/game.glang"));
                    sources->setCurrentRow(0);
                }
                click(kTechnical);
                Require(window.pages_->currentIndex() == 1, "technical page not opened");
                click(kTechnical);
                Require(window.pages_->currentIndex() == 0, "scene page not restored");

                const auto field_id = [&](const std::string& key, int component) {
                    for (auto i = 0u; i < window.property_fields_.size(); ++i) {
                        const auto& field = window.property_fields_[i];
                        if (field.key == key && field.component == component) {
                            return 1001 + int(i) * 2;
                        }
                    }
                    throw std::runtime_error("property input missing");
                };
                edit(field_id("move_speed", -1), QStringLiteral("6.25"));
                click(kApplyProperties);
                Require(window.core_.scenes().document()->settings.move_speed == 6.25f, "GUI property not applied");
                click(kUndo);
                Require(window.core_.scenes().document()->settings.move_speed != 6.25f, "GUI undo failed");
                click(kRedo);
                Require(window.core_.scenes().document()->settings.move_speed == 6.25f, "GUI redo failed");

                // 一覧選択と未適用入力を確認し、同時編集後に古いフォームを適用しない。
                QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  list->visualItemRect(list->item(1)).center());
                const auto x_id = field_id("position", 0);
                edit(x_id, QStringLiteral("1.5"));
                Require(window.form_dirty_, "typed property not marked pending");
                QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  list->visualItemRect(list->item(2)).center());
                Require(window.selected_object_ == "player", "selection discarded pending input");
                const auto before = window.core_.scenes().State();
                auto position = Value::Array{2.0, -1.0};
                if (std::string_view(game) == "3d") {
                    position = {2.0, 1.0, 3.0};
                }
                const auto submitted = window.core_.dispatcher().SubmitTyped(
                    "scene.set_properties",
                    SceneSet{before.document_id, before.revision, "player", {{"position", position}}}, NewId());
                Require(submitted.has_value(), "external edit rejected");
                window.core_.dispatcher().Pump();
                Require(window.core_.dispatcher().Lookup(submitted->operation_id).operation->error.has_value() == false,
                        "external edit validation failed");
                window.Refresh();
                Require(window.InputText(x_id) == "1.5", "external edit erased pending input");
                click(kApplyProperties);
                Require(window.form_dirty_, "conflict discarded pending input");
                Require(window.core_.scenes().document()->objects.front().position[0] == 2,
                        "conflict overwrote external edit");
                click(kPropertyRefresh);
                Require(window.form_dirty_ == false && window.InputText(x_id) == "2",
                        "refresh did not recover latest input");

                if (std::string_view(game) == "2d") {
                    const auto view = window.core_.SceneViewHandle();
                    const auto pixel = [&](gengine::Point2d world) {
                        auto rect = RECT{};
                        GetClientRect(view, &rect);
                        const auto state = window.core_.ViewportStatus();
                        return gengine::MakeMapping2d({{state.center[0], state.center[1]}, state.height}, rect.right,
                                                      rect.bottom)
                            .ToPixel(world);
                    };
                    const auto mouse = [&](UINT message, gengine::Point2d p) {
                        SendMessageW(view, message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0,
                                     MAKELPARAM(int(std::lround(p[0])), int(std::lround(p[1]))));
                    };
                    auto p = pixel({2, -1});
                    mouse(WM_LBUTTONDOWN, {p[0] + 44, p[1]});
                    mouse(WM_MOUSEMOVE, {p[0] + 60, p[1]});
                    Require(window.core_.ViewportStatus().transient, "native handle not captured");
                    Require(window.Control(x_id)->isEnabled() == false, "drag allowed numeric input");
                    SendMessageW(view, WM_KEYDOWN, VK_ESCAPE, 0);
                    Require(window.core_.ViewportStatus().transient == false, "Escape failed");
                    Require(window.InputText(x_id) == "2", "cancel left tentative coordinates");

                    // Qtの種類欄と追加ボタンから配置待ちへ入り、ビューで確定する。
                    p = pixel({0, 3});
                    mouse(WM_LBUTTONDOWN, p);
                    mouse(WM_LBUTTONUP, p);
                    click(kDeleteObject);
                    Require(window.core_.scenes().document()->objects.size() == 2, "GUI delete failed");
                    window.SelectIndex(kObjectKind, 1);
                    click(kAddObject);
                    Require(window.core_.ViewportStatus().transient, "GUI did not begin placement");
                    p = pixel({1, 3});
                    mouse(WM_LBUTTONDOWN, p);
                    mouse(WM_LBUTTONUP, p);
                    Require(window.core_.scenes().document()->objects.size() == 3, "click placement failed");
                    click(kUndo);
                    Require(window.core_.scenes().document()->objects.size() == 2, "placement undo failed");
                    click(kRedo);
                    Require(window.core_.scenes().document()->objects.size() == 3, "placement redo failed");
                }
                else {
                    const auto view = window.core_.SceneViewHandle();
                    const auto mapping = [&] {
                        auto rect = RECT{};
                        GetClientRect(view, &rect);
                        const auto s = window.core_.ViewportStatus();
                        return gengine::MakeMapping3d(
                            {{s.center[0], s.center[1], s.center[2]}, s.height, *s.yaw_degrees, *s.pitch_degrees},
                            rect.right, rect.bottom);
                    };
                    const auto mouse = [&](UINT message, gengine::Point2d p) {
                        SendMessageW(view, message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0,
                                     MAKELPARAM(int(std::lround(p[0])), int(std::lround(p[1]))));
                    };
                    const auto player_position = gengine::Point3d{2, 1, 3};
                    const auto center = mapping().ToPixel(player_position);
                    const auto direction = gengine::Axes3d(mapping(), player_position)[2].direction;
                    const auto start = gengine::Point2d{center[0] + direction[0] * 44, center[1] + direction[1] * 44};
                    mouse(WM_LBUTTONDOWN, start);
                    mouse(WM_MOUSEMOVE, {start[0] + direction[0] * 15, start[1] + direction[1] * 15});
                    Require(window.core_.ViewportCandidate().has_value(), "3D Z handle not captured");
                    const auto z_id = field_id("position", 2);
                    Require(window.Control(z_id)->isEnabled() == false && window.InputText(z_id) != "3",
                            "3D candidate not synchronized");
                    SendMessageW(view, WM_KEYDOWN, VK_ESCAPE, 0);
                    Require(window.InputText(z_id) == "3" && window.Control(z_id)->isEnabled(),
                            "3D cancel left candidate");
                    // GPU画像をQtの既存PNGデコーダーで読み、各ハンドルの色と位置を照合する。
                    const auto document = window.core_.scenes().State();
                    Require(window
                                .Invoke<PlayState>("viewport.capture",
                                                   CaptureRequest{{}, document.document_id, document.revision})
                                .has_value(),
                            "3D capture failed");
                    const auto png = window.core_.TakeCapture();
                    const auto image = QImage::fromData(png.data(), int(png.size()), "PNG");
                    Require(image.width() == 640 && image.height() == 360, "3D capture size");
                    const auto capture_mapping = gengine::MakeMapping3d(mapping().view, 640, 360);
                    const auto capture_center = capture_mapping.ToPixel(player_position);
                    const auto capture_axes = gengine::Axes3d(capture_mapping, player_position);
                    for (auto axis = 0u; axis < 3; ++axis) {
                        const auto& d = capture_axes[axis].direction;
                        const auto color = image.pixelColor(int(std::lround(capture_center[0] + 60 * d[0])),
                                                            int(std::lround(capture_center[1] + 60 * d[1])));
                        const auto channels = std::array{color.red(), color.green(), color.blue()};
                        Require(channels[axis] > 200 &&
                                    channels[axis] > *std::min_element(channels.begin(), channels.end()) + 60,
                                "3D GPU axis color/position mismatch");
                    }
                    // 3DでもQtの追加ボタンから水平面上のクリック配置へ進む。
                    const auto enemy = mapping().ToPixel({0, 2, -4});
                    mouse(WM_LBUTTONDOWN, enemy);
                    mouse(WM_LBUTTONUP, enemy);
                    Require(window.selected_object_ == "enemy", "3D click did not synchronize object list");
                    click(kDeleteObject);
                    window.SelectIndex(kObjectKind, 1);
                    click(kAddObject);
                    Require(window.core_.ViewportStatus().transient, "3D GUI placement not pending");
                    const auto placed = mapping().ToPixel({1, 2, -3});
                    mouse(WM_LBUTTONDOWN, placed);
                    mouse(WM_LBUTTONUP, placed);
                    Require(window.core_.scenes().document()->objects.size() == 3, "3D click placement failed");
                    const auto& object = window.core_.scenes().document()->objects.back();
                    Require(object.kind == "enemy_spawn" && std::abs(object.position[0] - 1) < .2 &&
                                object.position[1] == 2 && std::abs(object.position[2] + 3) < .2,
                            "3D placement differs from click plane");
                    click(kUndo);
                    Require(window.core_.scenes().document()->objects.size() == 2, "3D placement undo failed");
                    click(kRedo);
                    Require(window.core_.scenes().document()->objects.size() == 3, "3D placement redo failed");
                }
                click(kSaveScene);
                Require(window.core_.scenes().State().dirty == false, "GUI save failed");
                click(kReloadScene);
                window.resize(960, 800);
                QTest::qWait(100);
                window.ResizeView();
                auto physical = RECT{};
                GetClientRect(window.core_.SceneViewHandle(), &physical);
                const auto ratio = window.view_host_->devicePixelRatioF();
                Require(std::abs(physical.right - window.view_host_->width() * ratio) <= 1 &&
                            std::abs(physical.bottom - window.view_host_->height() * ratio) <= 1,
                        "Qt logical pixels and native rendering pixels differ");
                std::cout << game << " device_pixel_ratio=" << ratio << " native_view=" << physical.right << 'x'
                          << physical.bottom << '\n';
                Require(window.view_host_->width() >= 200 && window.view_host_->height() >= 200,
                        "minimum view overlaps panels");
                Require(window.screen()
                            ->grabWindow(window.winId())
                            .save(QString::fromStdWString((output / (std::string(game) + "-qt.png")).wstring())),
                        "GUI screenshot failed");
                // 実ボタンから同じビルドジョブを通し、完了した成果物と別プロセス管理を確認する。
                click(kBuild);
                Require(window.state_.active_job.has_value(), "GUI build was not accepted");
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                while (window.state_.active_job) {
                    Require(std::chrono::steady_clock::now() < deadline, "GUI build timed out");
                    QTest::qWait(50);
                    window.Refresh();
                }
                const auto built = window.Invoke<JobInfo>("job.get", JobRequest{*window.state_.last_job});
                Require(built && built->kind == "Build" && built->state == "Succeeded", "GUI build failed");
                VerifyBuild(window.core_.workspace() / Wide(*built->artifact), *built->artifact_hash);
                // 同じ入力をMCP adapterにも渡し、GUI起点と全成果物のハッシュまで一致させる。
                auto args = AsJson(Encode(BuildStart{window.state_.project->project_id, window.state_.project->revision,
                                                     window.state_.project->sources[0], window.document_state_.path}));
                args["_operation_id"] = NewId();
                const auto replies = mcp.Receive({Json{{"jsonrpc", "2.0"},
                                                       {"id", 2},
                                                       {"method", "tools/call"},
                                                       {"params", {{"name", "build.start"}, {"arguments", args}}}}
                                                      .dump()});
                const auto reply = ParseJson(replies.at(0)).at("result");
                Require(reply.value("isError", false) == false, "MCP parity build rejected");
                const auto mcp_job = reply.at("structuredContent").at("job_id").get<std::string>();
                deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                auto parity = JobInfo{};
                do {
                    Require(std::chrono::steady_clock::now() < deadline, "MCP parity build timed out");
                    QTest::qWait(30);
                    parity = *window.Invoke<JobInfo>("job.get", JobRequest{mcp_job});
                } while (parity.state == "Running" || parity.state == "Queued");
                Require(parity.state == "Succeeded" && parity.artifact_hash == built->artifact_hash &&
                            parity.input_hash == built->input_hash,
                        "GUI and MCP build outputs differ");
                window.Refresh();
                QCoreApplication::processEvents();
                // デスクトップ上の領域を撮り、Qt外のD3D子ウィンドウも含めて確認する。
                window.raise();
                window.activateWindow();
                QTest::qWait(100);
                const auto screen_origin = window.mapToGlobal(QPoint{});
                Require(window.screen()
                            ->grabWindow(0, screen_origin.x(), screen_origin.y(), window.width(), window.height())
                            .save(QString::fromStdWString((output / (std::string(game) + "-build.png")).wstring())),
                        "build screenshot failed");
                window.SelectIndex(kPlayMode, 1);
                window.Layout();
                click(kPlay);
                deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                while (window.core_.RunnerActive() == false) {
                    Require(std::chrono::steady_clock::now() < deadline, "GUI standalone timed out");
                    QTest::qWait(50);
                    window.Refresh();
                }
                Require(window.core_.PlayHandle() == nullptr, "standalone also created PIE");
                QTest::qWait(400);
                Require(window.core_.RunnerStatus().state == "Running", "standalone startup failed");
                Require(window.Control(kStopPlay)->isEnabled(), "standalone stop button remained disabled");
                Require(window.Control(kApplyProperties)->isEnabled() == false, "standalone enabled scene editing");
                click(kStopPlay);
                deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (window.core_.RunnerActive()) {
                    Require(std::chrono::steady_clock::now() < deadline, "GUI runner stop timed out");
                    QTest::qWait(20);
                }
                Require(window.core_.RunnerStatus().state == "Stopped", "GUI standalone stop failed");
                window.SelectIndex(kPlayMode, 0);
                window.Refresh();
                click(kClose);
                Require(window.state_.project.has_value() == false, "GUI project close failed");
            }
            std::cout << "Qt GUI: IME composition/commit, both scenes, property/history/save, pending edit/conflict, "
                         "native drag/cancel/add passed\n";
        }
    };
} // namespace

int main(int argc, char** argv)
{
    auto app = QApplication(argc, argv);
    if (argc != 3) {
        return 2;
    }
    const auto root = std::filesystem::path(app.arguments()[1].toStdWString());
    std::filesystem::create_directories(root);
    auto window = EditorWindow(root, app.arguments()[2].toStdWString(), false, false);
    auto status = 0;
    QTimer::singleShot(300, &window, [&] {
        try {
            QtEditorTest::Run(window, root);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            status = 1;
        }
        app.exit(status);
    });
    window.Run();
    return status;
}
