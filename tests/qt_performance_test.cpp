// 製品のQt画面と共通アクションで計測表示・編集・試遊・開閉の寿命を検査する。
#define GENGINE_EDITOR_TEST
#include "../apps/Editor/main.cpp"
#include <QScreen>
#include <QTest>
#include <QToolTip>
#include <ProcessSnapshot.h>
#include <psapi.h>
#include <fstream>
#include <iostream>

namespace
{
    Json NativeObjects()
    {
        // 現プロセスのハンドル種別とDLLだけを記録し、遅延初期化と開閉ごとの漏れを切り分ける。
        auto snapshot = HPSS{};
        if (PssCaptureSnapshot(GetCurrentProcess(), PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_NAME_INFORMATION, 0,
                               &snapshot) != ERROR_SUCCESS) {
            throw std::runtime_error("handle snapshot failed");
        }
        const auto release_snapshot = [](HPSS value) {
            PssFreeSnapshot(GetCurrentProcess(), value);
        };
        auto owned_snapshot =
            std::unique_ptr<std::remove_pointer_t<HPSS>, decltype(release_snapshot)>(snapshot, release_snapshot);
        auto marker = HPSSWALK{};
        if (PssWalkMarkerCreate(nullptr, &marker) != ERROR_SUCCESS) {
            throw std::runtime_error("handle walk failed");
        }
        const auto release_marker = [](HPSSWALK value) {
            PssWalkMarkerFree(value);
        };
        auto owned_marker =
            std::unique_ptr<std::remove_pointer_t<HPSSWALK>, decltype(release_marker)>(marker, release_marker);
        auto handles = Json::array();
        auto entry = PSS_HANDLE_ENTRY{};
        while (PssWalkSnapshot(snapshot, PSS_WALK_HANDLES, marker, &entry, sizeof(entry)) == ERROR_SUCCESS) {
            handles.push_back(
                {{"handle", reinterpret_cast<std::uintptr_t>(entry.Handle)},
                 {"type",
                  entry.TypeName
                      ? QString::fromWCharArray(entry.TypeName, entry.TypeNameLength / sizeof(wchar_t)).toStdString()
                      : ""},
                 {"name", entry.ObjectName
                              ? QString::fromWCharArray(entry.ObjectName, entry.ObjectNameLength / sizeof(wchar_t))
                                    .toStdString()
                              : ""}});
        }
        auto modules = std::array<HMODULE, 512>{};
        auto bytes = DWORD{};
        auto names = Json::array();
        if (EnumProcessModules(GetCurrentProcess(), modules.data(), sizeof(modules), &bytes)) {
            for (auto i = 0u; i < std::min<std::size_t>(modules.size(), bytes / sizeof(HMODULE)); ++i) {
                auto path = std::array<wchar_t, 32768>{};
                if (GetModuleFileNameW(modules[i], path.data(), static_cast<DWORD>(path.size()))) {
                    names.push_back(QString::fromWCharArray(path.data()).toStdString());
                }
            }
        }
        return {{"handles", handles}, {"modules", names}};
    }
    class QtEditorTest
    {
      public:
        template <class Output, class Input>
        static std::optional<Output> Call(EditorWindow& window, const char* name, const Input& request)
        {
            const auto result = window.Invoke<Output>(name, request, true);
            if (result.has_value() == false) {
                throw std::runtime_error(window.InputText(kDiagnostics));
            }
            return result;
        }
        static void Run(EditorWindow& window, const std::filesystem::path& output)
        {
            window.resize(1500, 900);
            auto* metrics_dock = window.findChild<QDockWidget*>("performanceDock");
            metrics_dock->show();
            QTest::qWait(50);
            metrics_dock->hide();
            const auto require = [](bool ok, const char* reason) {
                if (ok == false) {
                    throw std::runtime_error(reason);
                }
            };
            for (const auto* game : {"2d", "3d"}) {
                const auto created = Call<EditorState>(window, "project.create", ProjectCreateRequest{NewId(), game});
                require(created.has_value(), "create failed");
                const auto path = created->project->path;
                Call<EditorState>(window, "project.close",
                                  CloseRequest{created->project->project_id, created->project->revision});
                auto resources = std::vector<gengine::ProcessResources>{};
                auto retained = std::vector<std::uint64_t>{};
                auto window_history = Json::array();
                for (auto cycle = 0; cycle < 20; ++cycle) {
                    const auto opened = Call<EditorState>(window, "project.open", ProjectPathRequest{path});
                    require(opened.has_value(), "open failed");
                    const auto document = Call<DocumentInfo>(window, "document.open", DocumentOpen{"scene.json"});
                    require(document.has_value(), "document open failed");
                    window.Refresh();
                    window.core_.UpdatePreview();
                    window.ResizeView();
                    const auto position = std::string_view(game) == "2d" ? std::vector<double>{0.25, -3}
                                                                         : std::vector<double>{0.25, 2, 4};
                    const auto changed = Call<DocumentInfo>(
                        window, "scene.set_properties",
                        SceneSet{
                            document->document_id, document->revision, "player", {{"position", Encode(position)}}});
                    require(changed.has_value(), "edit failed");
                    const auto saved = Call<DocumentInfo>(window, "document.save",
                                                          DocumentRequest{changed->document_id, changed->revision});
                    require(saved.has_value(), "save failed");
                    const auto accepted =
                        Call<CompileAccepted>(window, "compile.start",
                                              CompileRequest{opened->project->project_id, opened->project->revision,
                                                             opened->project->sources[0]});
                    require(accepted.has_value(), "compile failed");
                    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    auto job = std::optional<JobInfo>{};
                    do {
                        QTest::qWait(20);
                        job = Call<JobInfo>(window, "job.get", JobRequest{accepted->job_id});
                        require(std::chrono::steady_clock::now() < deadline, "compile timed out");
                    } while (job && job->state != "Succeeded" && job->state != "Failed");
                    require(job && job->state == "Succeeded", "compile did not succeed");
                    const auto play = Call<PlayState>(
                        window, "play.start", PlayStart{job->job_id, 4, game, saved->document_id, saved->revision});
                    require(play.has_value(), "play failed");
                    window.Refresh();
                    window.ResizeView();
                    // 最初に計測画面の遅延初期化も済ませ、以後は同じ開閉条件で資源を比較する。
                    if (cycle == 0) {
                        auto* dock = window.findChild<QDockWidget*>("performanceDock");
                        require(dock != nullptr, "metrics dock missing");
                        window.resize(1500, 900);
                        dock->show();
                        QCoreApplication::processEvents();
                        auto* start = window.findChild<QPushButton*>("performance.start");
                        auto* refresh = window.findChild<QPushButton*>("performance.get");
                        require(start && refresh, "metrics buttons missing");
                        QTest::mouseClick(start, Qt::LeftButton);
                        // 表示自身の費用を実際のボタン操作で採取する。
                        for (auto query = 0; query < 40; ++query) {
                            QTest::qWait(50);
                            QTest::mouseClick(refresh, Qt::LeftButton);
                            Call<PlayState>(window, "play.pause", PlayRequest{play->play_id, false});
                        }
                        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                        auto report =
                            Call<gengine::PerformanceReport>(window, "performance.get", PlayRequest{play->play_id});
                        while (report && report->state != "Complete") {
                            require(std::chrono::steady_clock::now() < deadline, "GUI capture timed out");
                            QTest::qWait(100);
                            report =
                                Call<gengine::PerformanceReport>(window, "performance.get", PlayRequest{play->play_id});
                        }
                        require(report && report->collected == 300 && report->timings[9].samples > 0 &&
                                    report->timings[10].samples > 0,
                                "missing GUI samples");
                        std::ofstream(output / (std::string(game) + "-editor.json"))
                            << gengine::PerformanceJson(*report);
                        QTest::mouseClick(refresh, Qt::LeftButton);
                        QTest::qWait(50);
                        require(window.grab().save(QString::fromStdWString(
                                    (output / (std::string(game) + "-performance.png")).wstring())),
                                "capture failed");
                        dock->hide();
                    }
                    else {
                        Call<gengine::PerformanceReport>(window, "performance.start",
                                                         PerformanceStart{play->play_id, {2, 12}});
                        QTest::qWait(280);
                    }
                    const auto measured =
                        Call<gengine::PerformanceReport>(window, "performance.get", PlayRequest{play->play_id});
                    require(measured.has_value(), "capture result missing");
                    auto bytes = std::uint64_t{0};
                    for (const auto& counter : measured->counters) {
                        if (counter.name == "vm_owned_bytes" || counter.name == "host_reserved_bytes" ||
                            counter.name == "audio_pcm_bytes") {
                            bytes += counter.value;
                        }
                    }
                    retained.push_back(bytes);
                    Call<PlayState>(window, "play.stop", PlayRequest{play->play_id});
                    Call<EditorState>(window, "project.close",
                                      CloseRequest{opened->project->project_id, opened->project->revision});
                    window.Refresh();
                    // フォーカス・tooltipの一時ウィンドウを測定条件から外し、同じ閉じた画面で比較する。
                    QToolTip::hideText();
                    if (auto* focus = QApplication::focusWidget()) {
                        focus->clearFocus();
                    }
                    QTest::mouseMove(window.centralWidget(), QPoint(1, 1));
                    // 通知・worker関連ハンドルの遅延解放を待ち、比較する二地点は同じ静置時間に揃える。
                    QTest::qWait(cycle == 10 || cycle == 19 ? 15000 : 1000);
                    // この試験は一つのタイマーcallback内で反復するため、通常のイベントループが行う遅延破棄も流す。
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    QCoreApplication::processEvents();
                    resources.push_back(gengine::ReadProcessResources());
                    if (cycle == 0 || cycle == 10 || cycle == 19) {
                        std::ofstream(output / (std::string(game) + "-objects-" + std::to_string(cycle) + ".json"))
                            << NativeObjects().dump(2);
                    }
                    // 増加がアプリ所有のどのウィンドウに対応するか、失敗時にも照合できるよう残す。
                    auto windows = Json::array();
                    const auto visit = [](HWND handle, LPARAM data) -> BOOL {
                        auto process = DWORD{};
                        GetWindowThreadProcessId(handle, &process);
                        if (process == GetCurrentProcessId()) {
                            auto name = std::array<char, 256>{};
                            GetClassNameA(handle, name.data(), static_cast<int>(name.size()));
                            static_cast<Json*>(reinterpret_cast<void*>(data))
                                ->push_back({{"handle", reinterpret_cast<std::uintptr_t>(handle)},
                                             {"class", name.data()},
                                             {"parent", reinterpret_cast<std::uintptr_t>(GetParent(handle))}});
                        }
                        return TRUE;
                    };
                    EnumWindows(visit, reinterpret_cast<LPARAM>(&windows));
                    EnumChildWindows(window.window_, visit, reinterpret_cast<LPARAM>(&windows));
                    window_history.push_back(windows);
                    std::ofstream(output / (std::string(game) + "-windows.json")) << window_history.dump(2);
                    std::cout << game << " cycle=" << cycle << " handles=" << resources.back().handles
                              << " private=" << resources.back().private_bytes
                              << " gdi=" << resources.back().gdi_objects << " user=" << resources.back().user_objects
                              << std::endl;
                    require(resources.back().private_bytes <= 512ull * 1024 * 1024, "Editor memory budget");
                }
                auto rows = Json::array();
                for (const auto& resource : resources) {
                    rows.push_back(AsJson(Encode(resource)));
                }
                std::ofstream(output / (std::string(game) + "-cycles.json")) << rows.dump(2);
                const auto& begin = resources.at(10);
                const auto& end = resources.back();
                require(end.private_bytes <= begin.private_bytes + 16ull * 1024 * 1024,
                        "repeated private memory growth");
                require(end.handles <= begin.handles + 4, "repeated handle growth");
                require(end.gdi_objects <= begin.gdi_objects && end.user_objects <= begin.user_objects,
                        "repeated GUI resource growth");
                require(std::all_of(retained.begin(), retained.end(),
                                    [&](auto bytes) {
                                        return bytes == retained.front();
                                    }),
                        "retained runtime memory changed");
                std::cout << game << " 20 edit/play/close cycles; private_delta="
                          << std::int64_t(end.private_bytes) - std::int64_t(begin.private_bytes)
                          << " handles=" << begin.handles << '/' << end.handles << " gdi=" << begin.gdi_objects << '/'
                          << end.gdi_objects << " user=" << begin.user_objects << '/' << end.user_objects << '\n';
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
    const auto output = std::filesystem::path(app.arguments()[1].toStdWString());
    std::filesystem::create_directories(output);
    auto window = EditorWindow(output, app.arguments()[2].toStdWString(), false, false);
    auto status = 0;
    QTimer::singleShot(300, &window, [&] {
        try {
            QtEditorTest::Run(window, output);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            status = 1;
        }
        app.exit(status);
    });
    window.Run();
    return status;
}
