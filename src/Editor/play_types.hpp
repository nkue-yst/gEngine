#pragma once
#include "action_types.hpp"

namespace gengine::editor
{
    struct PlayStart
    {
        std::string job_id;
        std::uint32_t preset = 0;
        std::optional<std::string> game;
        std::optional<std::string> document_id;
        std::optional<std::uint64_t> expected_revision;
    };
    template <> struct Record<PlayStart>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("job_id", &PlayStart::job_id), MakeField("preset", &PlayStart::preset),
                              MakeField("game", &PlayStart::game), MakeField("document_id", &PlayStart::document_id),
                              MakeField("expected_revision", &PlayStart::expected_revision)};
        }
    };
    struct CaptureRequest
    {
        std::optional<std::string> play_id;
        std::optional<std::string> document_id;
        std::optional<std::uint64_t> expected_revision;
    };
    template <> struct Record<CaptureRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("play_id", &CaptureRequest::play_id),
                              MakeField("document_id", &CaptureRequest::document_id),
                              MakeField("expected_revision", &CaptureRequest::expected_revision)};
        }
    };
    struct PlayRequest
    {
        std::string play_id;
        std::optional<bool> paused;
        std::optional<bool> normal;
    };
    template <> struct Record<PlayRequest>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("play_id", &PlayRequest::play_id), MakeField("paused", &PlayRequest::paused),
                              MakeField("normal", &PlayRequest::normal)};
        }
    };
    struct PlayState
    {
        std::string play_id;
        std::string phase;
        bool paused = false;
        std::uint32_t preset = 0;
        std::uint64_t tick = 0;
        std::uint32_t outcome = 0;
        std::string diagnostic;
        std::uint64_t input_revision = 0;
        std::uint64_t vm_owned_bytes = 0;
        std::uint64_t update_ns = 0;
        std::uint64_t contact_ns = 0;
        std::uint64_t apply_ns = 0;
        std::uint32_t dropped_shots = 0;
        std::uint64_t dropped_time_us = 0;
        std::string game;
        std::optional<std::string> document_id;
    };
    template <> struct Record<PlayState>
    {
        static auto Fields()
        {
            return std::tuple{MakeField("play_id", &PlayState::play_id),
                              MakeField("phase", &PlayState::phase),
                              MakeField("paused", &PlayState::paused),
                              MakeField("preset", &PlayState::preset),
                              MakeField("tick", &PlayState::tick),
                              MakeField("outcome", &PlayState::outcome),
                              MakeField("diagnostic", &PlayState::diagnostic),
                              MakeField("input_revision", &PlayState::input_revision),
                              MakeField("vm_owned_bytes", &PlayState::vm_owned_bytes),
                              MakeField("update_ns", &PlayState::update_ns),
                              MakeField("contact_ns", &PlayState::contact_ns),
                              MakeField("apply_ns", &PlayState::apply_ns),
                              MakeField("dropped_shots", &PlayState::dropped_shots),
                              MakeField("dropped_time_us", &PlayState::dropped_time_us),
                              MakeField("game", &PlayState::game),
                              MakeField("document_id", &PlayState::document_id)};
        }
    };
} // namespace gengine::editor
