#include "game.hpp"
#include <cmath>
#include <stdexcept>

namespace gengine
{
    void Game::RefreshActors()
    {
        actors_.clear();
        if (HasActors() == false) {
            return;
        }
        // 列を借用している間はVMを呼ばず、判定と描画が共有する小さなスナップショットへ移す。
        const auto view = vm_.InspectPool("actors");
        if (view.capacity() > 1024) {
            throw std::runtime_error("actor pool exceeds 1024");
        }
        const auto x = view.Column<float>("position", 0);
        const auto y = view.Column<float>("position", 1);
        const auto px = view.Column<float>("previous", 0);
        const auto py = view.Column<float>("previous", 1);
        const auto z = mode_ == GameMode::k3d ? view.Column<float>("position", 2) : std::span<const float>{};
        const auto pz = mode_ == GameMode::k3d ? view.Column<float>("previous", 2) : std::span<const float>{};
        const auto r = view.Column<float>("color", 0);
        const auto g = view.Column<float>("color", 1);
        const auto b = view.Column<float>("color", 2);
        const auto radius = view.Column<float>("radius");
        const auto angle = view.Column<float>("angle");
        const auto visual = view.Column<std::uint32_t>("visual");
        const auto mask = view.Column<std::uint32_t>("mask");
        const auto opacity = actor_opacity_ ? view.Column<float>("opacity") : std::span<const float>{};
        for (auto row = std::size_t{0}; row < x.size(); ++row) {
            auto actor = Actor{{px[row], py[row], pz.empty() ? 0 : pz[row]},
                               {x[row], y[row], z.empty() ? 0 : z[row]},
                               {r[row], g[row], b[row]},
                               radius[row],
                               angle[row],
                               visual[row],
                               mask[row],
                               opacity.empty() ? 1.f : opacity[row]};
            for (const auto value : {actor.previous.x, actor.previous.y, actor.previous.z, actor.position.x,
                                     actor.position.y, actor.position.z, actor.color.x, actor.color.y, actor.color.z,
                                     actor.radius, actor.angle, actor.opacity}) {
                if (std::isfinite(value) == false) {
                    throw std::runtime_error("actor contains a non-finite value");
                }
            }
            if (actor.radius <= 0 || actor.radius > 16 ||
                actor.visual > (mode_ == GameMode::k2d ? (actor_opacity_ ? 33u : 17u) : 1u) || actor.opacity < 0 ||
                actor.opacity > 1 || actor.mask > 3 || r[row] < 0 || r[row] > 1 || g[row] < 0 || g[row] > 1 ||
                b[row] < 0 || b[row] > 1) {
                throw std::runtime_error("actor display or collision field out of range");
            }
            for (const auto value : {actor.previous.x, actor.previous.y, actor.previous.z, actor.position.x,
                                     actor.position.y, actor.position.z}) {
                if (std::abs(value) > 1000000) {
                    throw std::runtime_error("actor position exceeds world limit");
                }
            }
            actors_.push_back(actor);
        }
    }

    void Game::FindActorContacts(gvm::Vec3 previous, gvm::Vec3 player)
    {
        actor_hits_.clear();
        actor_contacts_.clear();
        if (HasActors() == false) {
            return;
        }
        const auto view = vm_.InspectPool("shots");
        const auto x = view.Column<float>("position", 0);
        const auto y = view.Column<float>("position", 1);
        const auto px = view.Column<float>("previous", 0);
        const auto py = view.Column<float>("previous", 1);
        const auto z = mode_ == GameMode::k3d ? view.Column<float>("position", 2) : std::span<const float>{};
        const auto pz = mode_ == GameMode::k3d ? view.Column<float>("previous", 2) : std::span<const float>{};
        for (auto shot = 0u; shot < x.size(); ++shot) {
            for (auto target = 0u; target < actors_.size(); ++target) {
                const auto& actor = actors_[target];
                if ((actor.mask & 1) != 0 && Contact({px[shot], py[shot], pz.empty() ? 0 : pz[shot]},
                                                     {x[shot], y[shot], z.empty() ? 0 : z[shot]}, actor.previous,
                                                     actor.position, actor.radius + .05f)) {
                    // 一発は最初の一個体へだけ通知する。順序は集合の行順で再現できる。
                    actor_hits_.push_back({shot, target});
                    break;
                }
            }
        }
        for (auto target = 0u; target < actors_.size(); ++target) {
            const auto& actor = actors_[target];
            if ((actor.mask & 2) != 0 &&
                Contact(previous, player, actor.previous, actor.position, actor.radius + .35f)) {
                actor_contacts_.push_back(target);
            }
        }
    }

    void Game::ApplyActorContacts()
    {
        if (HasActors() == false) {
            return;
        }
        // 個体と弾の削除はfinish_stepまで遅延させる契約。次の通知前に長さの違反を検出する。
        const auto length = [this](const char* name) {
            return vm_.InspectPool(name).size();
        };
        const auto shots = length("shots");
        const auto threats = length("threats");
        const auto validate = [&] {
            if (length("actors") != actors_.size() || length("shots") != shots || length("threats") != threats) {
                throw std::runtime_error("actor callbacks must defer structural changes until finish_step");
            }
        };
        for (const auto& hit : actor_hits_) {
            vm_.CallWithFuel(step_fuel_, *actor_hit_, hit[0], hit[1]);
            validate();
        }
        for (const auto target : actor_contacts_) {
            vm_.CallWithFuel(step_fuel_, *actor_contact_, target);
            validate();
        }
    }
} // namespace gengine
