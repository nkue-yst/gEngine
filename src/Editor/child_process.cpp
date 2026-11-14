#include "child_process.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace gengine::editor
{
    ChildProcess::~ChildProcess()
    {
        Stop();
    }

    void ChildProcess::Stop()
    {
        // 所有する子だけを停止し、ハンドルを閉じる前に終了を確認する。
        if (process.Valid() && WaitForSingleObject(process.Get(), 0) == WAIT_TIMEOUT) {
            TerminateJobObject(group.Get(), 1);
            WaitForSingleObject(process.Get(), INFINITE);
        }
    }

    void ChildProcess::Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                             const std::filesystem::path& directory)
    {
        // 子の標準出力・標準エラーを受け取り、読込み側は親だけが保持する。
        auto security = SECURITY_ATTRIBUTES{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE read_handle = nullptr;
        HANDLE write_handle = nullptr;
        if (CreatePipe(&read_handle, &write_handle, &security, 0) == FALSE) {
            throw std::runtime_error("cannot create child process output pipe");
        }
        pipe.Reset(read_handle);
        auto writer = Handle(write_handle);
        if (SetHandleInformation(pipe.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE) {
            throw std::runtime_error("cannot isolate child process output pipe");
        }

        // 標準入力は使わず、Editor終了時に子も停止するJob Objectを用意する。
        auto input_handle = Handle(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        group.Reset(CreateJobObjectW(nullptr, nullptr));
        auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (input_handle.Valid() == false || group.Valid() == false ||
            SetInformationJobObject(group.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE) {
            throw std::runtime_error("cannot prepare child process process lifetime");
        }

        // MCPの標準入出力を子に引き継がない。必要な2ハンドルだけを明示する。
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        auto storage = std::vector<unsigned char>(bytes);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) == FALSE) {
            throw std::runtime_error("cannot prepare child process handle list");
        }

        struct AttributeCleanup
        {
            LPPROC_THREAD_ATTRIBUTE_LIST value;
            ~AttributeCleanup()
            {
                DeleteProcThreadAttributeList(value);
            }
        } cleanup{attributes};
        auto inherited = std::array<HANDLE, 2>{writer.Get(), input_handle.Get()};
        if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
                                      sizeof(inherited), nullptr, nullptr) == FALSE) {
            throw std::runtime_error("cannot restrict child process handles");
        }

        // 起動引数はシェルを経由せず渡し、一時入力のフォルダーを作業場所にする。
        auto startup = STARTUPINFOEXW{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input_handle.Get();
        startup.StartupInfo.hStdOutput = writer.Get();
        startup.StartupInfo.hStdError = writer.Get();
        startup.lpAttributeList = attributes;
        auto information = PROCESS_INFORMATION{};
        auto command = QuoteArgument(executable.native());
        for (const auto& argument : arguments) {
            command += L" " + QuoteArgument(argument);
        }

        // 先に停止状態で生成し、Job Objectへ所属させてから実行を始める。
        if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                           EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                           directory.c_str(), &startup.StartupInfo, &information) == FALSE) {
            throw std::runtime_error("cannot start the bundled executable");
        }
        process.Reset(information.hProcess);
        auto thread = Handle(information.hThread);
        if (AssignProcessToJobObject(group.Get(), process.Get()) == FALSE) {
            TerminateProcess(process.Get(), 1);
            throw std::runtime_error("cannot attach child process to lifetime group");
        }

        if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1)) {
            throw std::runtime_error("cannot resume child process");
        }
        started = std::chrono::steady_clock::now();
    }

    void ChildProcess::Drain()
    {
        auto buffer = std::array<char, 4096>{};
        DWORD available = 0;

        // ログを制限しても読み捨てを続け、満杯のパイプで子が止まることを防ぐ。
        while (pipe.Valid() && PeekNamedPipe(pipe.Get(), nullptr, 0, nullptr, &available, nullptr) != FALSE &&
               available != 0) {
            DWORD count = 0;
            if (::ReadFile(pipe.Get(), buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())),
                           &count, nullptr) == FALSE ||
                count == 0) {
                break;
            }
            log.append(buffer.data(), std::min<std::size_t>(count, 64 * 1024 - log.size()));
        }
    }
} // namespace gengine::editor
