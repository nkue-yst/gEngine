#pragma once
#include "child_process.hpp"
#include "scene_document.hpp"
#include <atomic>
#include <future>
#include <map>

namespace gengine::editor
{
    struct BuildInput
    {
        std::filesystem::path project_root;
        std::filesystem::path tools;
        std::filesystem::path compiler;
        std::filesystem::path directory;
        std::string source_name;
        std::string source;
        std::string scene;
        std::string project_hash;
    };
    struct BuildOutput
    {
        std::string input_hash;
        std::string manifest_hash;
        std::string diagnostic;
        std::string phase;
        std::string game;
        bool succeeded = false;
    };

    // 重いファイル処理と子プロセスの待機はworkerで行い、Editorの状態は触らない。
    // 所有スレッドが結果を回収してから、完成したフォルダーだけを公開する。
    class BuildPipeline
    {
      public:
        explicit BuildPipeline(BuildInput input);
        ~BuildPipeline();
        void Cancel();
        std::string Phase() const;
        bool Ready() const;
        BuildOutput Get();

      private:
        BuildOutput Run();
        void CheckCancelled() const;
        void RunTool(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                     const std::filesystem::path& cwd);
        void TrackFile(const std::filesystem::path& file);
        BuildInput input_;
        std::vector<std::filesystem::path> temporary_files_;
        std::vector<std::filesystem::path> temporary_directories_;
        std::atomic<bool> cancelled_ = false;
        std::atomic<unsigned> phase_ = 0;
        std::future<BuildOutput> result_;
    };

    // 起動前に成功時のmanifestと全実行ファイルを照合する。欠落・改変は起動しない。
    std::string VerifyBuild(const std::filesystem::path& directory, std::string_view manifest_hash);
} // namespace gengine::editor
