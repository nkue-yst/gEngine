#pragma once
#include "platform.hpp"
#include <chrono>
#include <vector>

namespace gengine::editor
{
    // コンパイラー・packツール・GameRunnerを同じ所有規則で起動する。
    // 引数はシェルを通さず、ログは上限付きで回収し、親の終了時は子も終了する。
    struct ChildProcess
    {
      public:
        ~ChildProcess();
        void Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                   const std::filesystem::path& directory);
        void Drain();
        void Stop();

        Handle process;
        Handle group;
        Handle pipe;
        std::string log;
        std::chrono::steady_clock::time_point started;
    };
} // namespace gengine::editor
