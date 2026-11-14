#include "editor_core.hpp"
#include <QApplication>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <iostream>
#include <psapi.h>

using namespace gengine::editor;
namespace
{
    template <class Output, class Input> Output Call(EditorCore& core, const char* name, Input input)
    {
        const auto result = core.dispatcher().SubmitTyped(name, input, NewId());
        if (result.has_value() == false) {
            throw std::runtime_error(result.error().reason);
        }
        core.dispatcher().Pump();
        const auto operation = core.dispatcher().Lookup(result->operation_id).operation;
        if (operation->error) {
            throw std::runtime_error(operation->error->reason);
        }
        return Decode<Output>(operation->result).value();
    }
} // namespace

int main(int argc, char** argv)
{
    try {
        auto app = QApplication(argc, argv);
        if (argc != 3) {
            throw std::invalid_argument("workspace/compiler required");
        }
        const auto arguments = app.arguments();
        const auto root = std::filesystem::path(arguments[1].toStdWString());
        std::filesystem::create_directories(root);
        auto core = EditorCore(root, std::filesystem::path(arguments[2].toStdWString()));
        auto window = QWidget{};
        auto* layout = new QVBoxLayout(&window);
        auto* input = new QLineEdit(QString::fromUtf8("日本語入力の確認"));
        layout->addWidget(input);
        auto* host = new QWidget;
        host->setAttribute(Qt::WA_NativeWindow);
        layout->addWidget(host, 1);
        window.resize(960, 800);
        window.show();
        core.SetPlayParent(reinterpret_cast<HWND>(host->winId()));

        // 同じ子HWNDへ更新し、Qtの親破棄より先にRuntimeを終了する。
        auto status = 0;
        QTimer::singleShot(100, &window, [&] {
            try {
                for (const auto* game : {"2d", "3d"}) {
                    auto project = Call<EditorState>(core, "project.create", ProjectCreateRequest{NewId(), game});
                    Call<DocumentInfo>(core, "document.open", DocumentOpen{"scene.json", {}, {}});
                    core.UpdatePreview();
                    const auto handle = core.SceneViewHandle();
                    auto rect = RECT{};
                    GetClientRect(reinterpret_cast<HWND>(host->winId()), &rect);
                    MoveWindow(handle, 0, 0, rect.right, rect.bottom, TRUE);
                    ShowWindow(handle, SW_SHOW);
                    for (auto i = 0; i < 24; ++i) {
                        core.UpdatePreview();
                        if (handle != core.SceneViewHandle()) {
                            throw std::runtime_error("preview HWND changed");
                        }
                    }
                    auto memory = PROCESS_MEMORY_COUNTERS_EX{};
                    memory.cb = sizeof(memory);
                    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                             sizeof(memory)) == FALSE) {
                        throw std::runtime_error("cannot read memory");
                    }
                    std::cout << game << " private_bytes=" << memory.PrivateUsage
                              << " working_set=" << memory.WorkingSetSize << '\n';
                    if (memory.PrivateUsage > 512ull * 1024 * 1024) {
                        throw std::runtime_error("Qt host exceeds 512 MiB private bytes budget");
                    }
                    const auto scene = core.scenes().State();
                    Call<EditorState>(
                        core, "project.close",
                        CloseRequest{project.project->project_id, project.project->revision, {}, scene.revision});
                }
                std::cout << "Qt host: 2D/3D stable child window, explicit shutdown passed\n";
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
                status = 1;
            }
            core.Shutdown();
            app.exit(status);
        });
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
