#include "game.hpp"
#include <stdexcept>

namespace gengine
{
    void Game::ResolveProfile()
    {
        auto version = std::optional<gvm::Function<std::uint32_t()>>{};
        try {
            version = vm_.ResolveFunction<std::uint32_t()>("profile_api_version");
        } catch (const gvm::Error& error) {
            if (std::string_view(error.what()) != "ExportNotFound") {
                throw;
            }
        }
        if (version.has_value() == false) {
            return;
        }
        if (vm_.CallWithFuel(step_fuel_, *version) != 1) {
            throw std::invalid_argument("unsupported profile API version");
        }
        profile_schema_ = vm_.ResolveFunction<std::uint32_t()>("profile_schema");
        profile_count_ = vm_.ResolveFunction<std::uint32_t()>("profile_count");
        profile_get_ = vm_.ResolveFunction<std::uint32_t(std::uint32_t)>("profile_get");
        profile_set_ = vm_.ResolveFunction<bool(std::uint32_t, std::uint32_t)>("profile_set");
        profile_accept_ = vm_.ResolveFunction<bool(std::uint32_t, std::uint32_t)>("profile_accept");
        profile_action_count_ = vm_.ResolveFunction<std::uint32_t()>("profile_action_count");
        profile_action_label_ = vm_.ResolveFunction<std::uint32_t(std::uint32_t)>("profile_action_label");
        profile_action_rank_ = vm_.ResolveFunction<std::uint32_t(std::uint32_t)>("profile_action_rank");
        profile_action_maximum_ = vm_.ResolveFunction<std::uint32_t(std::uint32_t)>("profile_action_maximum");
        profile_action_cost_ = vm_.ResolveFunction<std::uint32_t(std::uint32_t)>("profile_action_cost");
        profile_action_enabled_ = vm_.ResolveFunction<bool(std::uint32_t)>("profile_action_enabled");
        profile_action_ = vm_.ResolveFunction<bool(std::uint32_t)>("profile_action");
        try {
            profile_reset_ = vm_.ResolveFunction<bool()>("profile_reset");
        } catch (const gvm::Error& error) {
            if (std::string_view(error.what()) != "ExportNotFound") {
                throw;
            }
        }
    }

    void Game::ApplyProfile()
    {
        if (profile_.count == 0) {
            return;
        }
        if (profile_.count > profile_.values.size() || profile_.schema == 0) {
            throw std::invalid_argument("invalid profile shape");
        }
        for (auto index = 0u; index < profile_.count; ++index) {
            if (vm_.CallWithFuel(step_fuel_, *profile_set_, index, profile_.values[index]) == false) {
                throw std::invalid_argument("game rejected profile value");
            }
        }
        if (vm_.CallWithFuel(step_fuel_, *profile_accept_, profile_.schema, profile_.count) == false) {
            throw std::invalid_argument("game rejected profile schema or values");
        }
    }

    void Game::CaptureProfile()
    {
        if (HasProfile() == false) {
            return;
        }
        auto candidate = ProfileData{};
        candidate.schema = vm_.CallWithFuel(step_fuel_, *profile_schema_);
        candidate.count = vm_.CallWithFuel(step_fuel_, *profile_count_);
        if (candidate.schema == 0 || candidate.count == 0 || candidate.count > candidate.values.size()) {
            throw std::invalid_argument("invalid game profile shape");
        }
        for (auto index = 0u; index < candidate.count; ++index) {
            candidate.values[index] = vm_.CallWithFuel(step_fuel_, *profile_get_, index);
        }
        // 部分取得の失敗で直前の正常な記録を失わない。
        profile_ = candidate;
    }

    bool Game::RestoreProfile(const ProfileData& value)
    {
        if (HasProfile() == false || state_.phase != Phase::kStart || value.count == 0) {
            return false;
        }
        const auto previous = profile_;
        profile_ = value;
        try {
            Reset();
            return true;
        } catch (const std::exception&) {
            profile_ = previous;
            Reset();
            return false;
        }
    }

    std::vector<ProfileAction> Game::ProfileActions()
    {
        auto result = std::vector<ProfileAction>{};
        if (HasProfile() == false || state_.phase != Phase::kStart) {
            return result;
        }
        const auto count = vm_.CallWithFuel(step_fuel_, *profile_action_count_);
        if (count > 4) {
            throw std::invalid_argument("profile actions exceed four");
        }
        for (auto index = 0u; index < count; ++index) {
            auto action = ProfileAction{};
            action.label = vm_.CallWithFuel(step_fuel_, *profile_action_label_, index);
            action.rank = vm_.CallWithFuel(step_fuel_, *profile_action_rank_, index);
            action.maximum = vm_.CallWithFuel(step_fuel_, *profile_action_maximum_, index);
            action.cost = vm_.CallWithFuel(step_fuel_, *profile_action_cost_, index);
            action.enabled = vm_.CallWithFuel(step_fuel_, *profile_action_enabled_, index);
            if (action.label >= 256 || action.maximum == 0 || action.rank > action.maximum) {
                throw std::invalid_argument("invalid profile action");
            }
            result.push_back(action);
        }
        return result;
    }

    bool Game::ActOnProfile(std::uint32_t index)
    {
        const auto actions = ProfileActions();
        if (index >= actions.size() || actions[index].enabled == false) {
            return false;
        }
        const auto previous = profile_;
        try {
            const auto accepted = vm_.CallWithFuel(step_fuel_, *profile_action_, index);
            if (accepted) {
                CaptureProfile();
            }
            // 拒否や部分更新も通常の初期化へ戻し、購入前後の戦闘状態を混ぜない。
            Reset();
            return accepted;
        } catch (const std::exception& error) {
            profile_ = previous;
            Fail(error.what());
            return false;
        }
    }

    bool Game::ResetProfile()
    {
        if (HasProfileReset() == false || state_.phase != Phase::kStart) {
            return false;
        }
        const auto previous = profile_;
        try {
            const auto accepted = vm_.CallWithFuel(step_fuel_, *profile_reset_);
            if (accepted) {
                CaptureProfile();
            }
            // 拒否時の部分更新も破棄し、通常開始と同じ初期化で画面と進行値を揃える。
            Reset();
            return accepted;
        } catch (const std::exception& error) {
            profile_ = previous;
            Fail(error.what());
            return false;
        }
    }
} // namespace gengine
