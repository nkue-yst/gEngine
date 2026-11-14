// 配置済みツールを使い、製品と同じ画面のボタンから一連の制作操作を通す。
#define GENGINE_EDITOR_TEST
#include "../apps/Editor/main.cpp"
#include <QTest>
#include <iostream>

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
        static void Run(EditorWindow& window, const std::filesystem::path& root)
        {
            const auto click = [&](int id) {
                Require(window.Control(id)->isEnabled(), "workflow button is disabled");
                QTest::mouseClick(window.Control(id), Qt::LeftButton);
                QCoreApplication::processEvents();
            };
            const auto wait = [&](auto condition) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
                while (condition() == false) {
                    Require(std::chrono::steady_clock::now() < deadline, "workflow timed out");
                    QTest::qWait(10);
                    window.Refresh();
                }
            };
            const auto job = [&] {
                wait([&] {
                    return window.state_.active_job.has_value() == false;
                });
                return *window.Invoke<JobInfo>("job.get", JobRequest{*window.state_.last_job});
            };
            const auto capture = [&](const std::filesystem::path& path) {
                const auto state = window.core_.PlayStatus();
                Require(window.Invoke<PlayState>("viewport.capture", CaptureRequest{state.play_id, {}, {}}).has_value(),
                        "PIE capture failed");
                const auto bytes = window.core_.TakeCapture();
                WriteNewFile(path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            };

            for (const auto* game : {"2d", "3d"}) {
                const auto name = std::string("shooter") + game;
                auto report = Json::object();
                qobject_cast<QLineEdit*>(window.Control(kPath))
                    ->setText(QString::fromStdString(name + "/project.json"));
                click(kOpen);
                Require(window.state_.project.has_value(), "relocated project open failed");
                click(kSceneOpen);

                // 値の適用・取消し・やり直しを実入力で通し、保存後に同じ値を読み直す。
                qobject_cast<QListWidget*>(window.Control(kObjects))->setCurrentRow(0);
                auto field_id = 0;
                for (auto i = 0u; i < window.property_fields_.size(); ++i) {
                    if (window.property_fields_[i].key == "move_speed") {
                        field_id = 1001 + int(i) * 2;
                    }
                }
                Require(field_id != 0, "move speed input missing");
                auto* field = qobject_cast<QLineEdit*>(window.Control(field_id));
                field->setFocus();
                field->selectAll();
                QTest::keyClicks(field, "6.25");
                click(kApplyProperties);
                click(kUndo);
                report["undo_speed"] = window.core_.scenes().document()->settings.move_speed;
                click(kRedo);
                click(kSaveScene);
                click(kReloadScene);
                Require(window.core_.scenes().document()->settings.move_speed == 6.25f, "GUI saved value lost");
                report["scene_hash"] = Sha256(ReadFile(root / name / "scene.json", 1024 * 1024));

                // 失敗診断も比較できるよう、同じ不正ソースを保存してから元へ戻す。
                const auto source = root / name / "game.glang";
                const auto original = ReadFile(source, 1024 * 1024);
                WriteAtomicFile(source, "export fn main() -> i32 { return true; }\n");
                click(kCompile);
                const auto failed = job();
                Require(failed.state == "Failed", "GUI invalid compile succeeded");
                report["diagnostics"] = AsJson(Encode(failed.diagnostics));
                WriteAtomicFile(source, original);
                click(kCompile);
                Require(job().state == "Succeeded", "GUI recompile failed");

                // 通常開始から移動と射撃でクリアし、帰還と次の開始まで実ウィンドウ入力を送る。
                window.SelectIndex(kPlayMode, 0);
                click(kPlay);
                Require(window.core_.PlayStatus().phase == "Start", "GUI PIE not at normal start");
                capture(root / (name + "-start.png"));
                const auto key = [&](WPARAM value, bool down) {
                    SendMessageW(window.core_.PlayHandle(), down ? WM_KEYDOWN : WM_KEYUP, value, 0);
                };
                key(VK_RETURN, true);
                key(VK_RETURN, false);
                key(VK_SPACE, true);
                wait([&] {
                    const auto state = window.core_.PlayStatus();
                    if (state.paused) {
                        key('P', true);
                        key('P', false);
                    }
                    // フォーカス喪失で解除された保持キーも、通常入力として再送する。
                    key(VK_SPACE, true);
                    const auto right = (state.tick + 18) % 72 < 36;
                    key('D', right);
                    key('A', right == false);
                    return state.phase != "Playing";
                });
                const auto result = window.core_.PlayStatus();
                std::cout << game << " PIE result " << ToJson(Encode(result)) << '\n';
                capture(root / (name + "-result.png"));
                Require(result.phase == "Result" && result.outcome == 1, "GUI normal play did not clear");
                report["outcome"] = result.outcome;
                key(VK_ESCAPE, true);
                key(VK_ESCAPE, false);
                Require(window.core_.PlayStatus().phase == "Start", "GUI return failed");
                capture(root / (name + "-returned.png"));
                key(VK_RETURN, true);
                key(VK_RETURN, false);
                Require(window.core_.PlayStatus().phase == "Playing", "GUI second start failed");
                click(kStopPlay);
                Require(window.core_.PlayStatus().phase == "Stopped", "GUI PIE stop failed");
                Require(Sha256(ReadFile(root / name / "scene.json", 1024 * 1024)) ==
                            report["scene_hash"].get<std::string>(),
                        "PIE wrote the saved scene");

                click(kBuild);
                const auto built = job();
                Require(built.state == "Succeeded", "GUI relocated build failed");
                report["input_hash"] = *built.input_hash;
                report["artifact_hash"] = *built.artifact_hash;
                report["artifact"] = *built.artifact;

                // GUIの実行方式選択から子を起動し、通常終了と明示停止を区別する。
                window.SelectIndex(kPlayMode, 1);
                window.Layout();
                click(kPlay);
                wait([&] {
                    return window.core_.RunnerActive();
                });
                auto child = std::pair<DWORD, HWND>{window.core_.RunnerStatus().process_id, nullptr};
                wait([&] {
                    EnumWindows(
                        [](HWND handle, LPARAM data) -> BOOL {
                            auto& value = *reinterpret_cast<std::pair<DWORD, HWND>*>(data);
                            auto pid = DWORD{};
                            GetWindowThreadProcessId(handle, &pid);
                            auto name = std::array<wchar_t, 128>{};
                            GetClassNameW(handle, name.data(), static_cast<int>(name.size()));
                            if (pid == value.first && std::wstring_view(name.data()) == L"gEnginePlayView") {
                                value.second = handle;
                            }
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&child));
                    return child.second != nullptr;
                });
                PostMessageW(child.second, WM_CLOSE, 0, 0);
                wait([&] {
                    return window.core_.RunnerActive() == false;
                });
                report["natural_exit"] = window.core_.RunnerStatus().state;
                report["exit_code"] = *window.core_.RunnerStatus().exit_code;
                Require(report["natural_exit"] == "Exited" && report["exit_code"] == 0, "GUI child exit failed");
                click(kPlay);
                wait([&] {
                    return window.core_.RunnerActive();
                });
                click(kStopPlay);
                wait([&] {
                    return window.core_.RunnerActive() == false;
                });
                report["stopped"] = window.core_.RunnerStatus().state;
                Require(report["stopped"] == "Stopped", "GUI child stop failed");
                window.SelectIndex(kPlayMode, 0);
                window.Refresh();
                Require(window.grab().save(QString::fromStdWString((root / (name + "-gui.png")).wstring())),
                        "workflow image failed");
                click(kClose);
                WriteNewFile(root / (name + "-gui.json"), report.dump(2));
                std::cout << game << " GUI relocated workflow passed\n";
            }
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
