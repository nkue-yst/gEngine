#pragma once
#include "action_types.hpp"

namespace gengine::editor
{
    struct BuildStart
    {
        std::string project_id;
        std::uint64_t expected_revision = 0;
        std::string source;
        std::string scene;
        std::optional<std::string> choice;
        std::optional<std::uint64_t> document_revision;
    };
    template <> struct Record<BuildStart>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("project_id", &BuildStart::project_id),
                              MakeField("expected_revision", &BuildStart::expected_revision),
                              MakeField("source", &BuildStart::source),
                              MakeField("scene", &BuildStart::scene),
                              MakeField("choice", &BuildStart::choice),
                              MakeField("document_revision", &BuildStart::document_revision)};
        }
    };
    struct RunnerRequest
    {
        std::string run_id;
    };
    template <> struct Record<RunnerRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("run_id", &RunnerRequest::run_id)};
        }
    };
    struct RunnerState
    {
        std::string run_id;
        std::string job_id;
        std::string state = "Stopped";
        std::optional<std::uint32_t> exit_code;
        std::string diagnostic;
        std::uint32_t process_id = 0;
    };
    template <> struct Record<RunnerState>
    {
        static auto Fields()
        {
            return std::tuple{
                MakeField("run_id", &RunnerState::run_id),         MakeField("job_id", &RunnerState::job_id),
                MakeField("state", &RunnerState::state),           MakeField("exit_code", &RunnerState::exit_code),
                MakeField("diagnostic", &RunnerState::diagnostic), MakeField("process_id", &RunnerState::process_id)};
        }
    };
} // namespace gengine::editor
