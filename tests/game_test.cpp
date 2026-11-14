#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool ok, const char* reason)
    {
        if (ok == false) {
            throw std::runtime_error(reason);
        }
    }
} // namespace
template <class Vector> void CheckGame(const std::vector<std::uint8_t>& bytes, gengine::GameMode mode)
{
    const auto is_3d = mode == gengine::GameMode::k3d;
    // 編集値を通常のVM初期化と実更新へ通し、既定値の残存を検出する。
    auto scene = gengine::DefaultScene(is_3d);
    scene.settings.player_hp = 9;
    scene.settings.enemy_hp = 4;
    scene.settings.move_speed = 6;
    scene.settings.fire_interval_seconds = 0.2f;
    scene.settings.bounds_min[0] = -2;
    scene.settings.bounds_max[0] = 2;
    scene.objects[0].position[0] = 1;
    scene.objects[1].position[0] = 1;
    scene.objects[1].fire_interval_seconds = 10;
    auto edited = gengine::Game(bytes, 1000000, mode, scene);
    Require(edited.state().hp == 9 && edited.state().enemy_hp == 4 && edited.state().player.x == 1,
            "edited health and spawn");
    Require(edited.EnemyPosition().x == 1, "edited enemy collision position");
    // スティックの倒し量をVMまで維持し、不正な軸は更新前に拒否する。
    auto analog_game = gengine::Game(bytes, 1000000, mode, scene);
    analog_game.Begin();
    const auto origin = analog_game.state().player;
    auto analog_input = gengine::InputState{};
    analog_input.horizontal = .25f;
    analog_input.vertical = .5f;
    analog_game.Step(analog_input);
    Require(std::abs(analog_game.state().player.x - origin.x - .025f) < .00001f &&
                std::abs((is_3d ? origin.z - analog_game.state().player.z : analog_game.state().player.y - origin.y) -
                         .05f) < .00001f,
            "analog magnitude was normalized away");
    analog_input.horizontal = std::numeric_limits<float>::infinity();
    analog_game.Step(analog_input);
    Require(analog_game.state().phase == gengine::Phase::kFaulted, "nonfinite input accepted");

    edited.Begin();
    edited.Step({false, true});
    Require(std::abs(edited.state().player.x - 1.1f) < 0.00001f, "edited movement speed");
    for (auto tick = 0; tick < 30; ++tick) {
        edited.Step({false, true});
    }
    Require(edited.state().player.x == 2, "edited movement bounds");
    edited.Reset(2);
    Require(edited.state().enemy_hp == 1, "preset applied after scene");
    edited.Reset();
    Require(edited.state().enemy_hp == 4 && edited.state().hp == 9 && edited.state().player.x == 1,
            "normal reset restores scene snapshot");
    edited.Begin();
    for (auto tick = 0; tick < 12; ++tick) {
        edited.Step({false, false, false, false, true});
    }
    Require(edited.Dots().size() == 1, "edited player and enemy firing intervals");
    edited.Step({false, false, false, false, true});
    Require(edited.Dots().size() == 2, "edited second shot timing");
    for (auto tick = 0; tick < 180 && edited.state().phase == gengine::Phase::kPlaying; ++tick) {
        edited.Step({false, false, false, false, true});
    }
    Require(edited.state().outcome == 1, "edited enemy defeated by real shots");
    edited.Reset();
    edited.Begin();
    Require(edited.state().player.x == 1 && edited.state().enemy_hp == 4, "edited loop restart");
    auto game = gengine::Game(bytes, 1000000, mode);
    Require(game.state().phase == gengine::Phase::kStart, "initial title");
    game.Begin();
    for (auto i = 0; i < 240; ++i) {
        game.Step({false, false, false, false, true});
    }
    Require(game.state().phase == gengine::Phase::kResult, "normal result");
    Require(game.state().diagnostic.empty(), "VM fault");
    game.Reset();
    Require(game.Dots().empty() && game.state().hp == 3, "reset state");
    // 斜め入力も速度5に揃え、画面の移動限界で止まる。
    game.Begin();
    game.Step({false, true, true, false, false});
    const auto position = game.state().player;
    Require(std::abs(std::hypot(position.x, is_3d ? position.z - 4 : position.y + 3) - 5.0f / 60) < 0.00001f,
            "diagonal speed");
    for (auto i = 0; i < 180; ++i) {
        game.Step({false, true, true, false, false});
    }
    Require(game.state().player.x == 8 && (is_3d ? game.state().player.z == -6 : game.state().player.y == 4.5f),
            "movement bounds");
    game.Reset();
    game.Begin();
    game.Pause(true);
    game.Advance(1, {});
    Require(game.state().tick == 0, "paused tick");
    game.Pause(false);
    game.Advance(1, {});
    Require(game.state().tick == 4 && game.state().dropped_seconds > 0.9, "catch-up bound");
    game.Reset(2);
    for (auto i = 0; i < 60; ++i) {
        game.Step({false, false, false, false, true});
    }
    Require(game.state().outcome == 1, "clear preset requires actual hit");
    game.Reset(3);
    for (auto i = 0; i < 120; ++i) {
        game.Step({});
    }
    Require(game.state().outcome == 2, "failure preset requires actual hit");
    game.Reset(4);
    Require(game.Dots().size() == 1024, "load preset");
    game.Step({});
    Require(game.state().diagnostic.empty(), "load update fault");
    Require(gengine::Game::Contact({-2, 0}, {2, 0}, {0, 0}, {0, 0}, 0.1f), "swept hit");
    Require(gengine::Game::Contact({-2, 1}, {2, 1}, {0, 0}, {0, 0}, 0.1f) == false, "swept miss");
    // 実行予算超過で途中の要求を捨て、resetで開始状態へ戻せる。
    auto failing = gengine::Game(bytes, 1, mode);
    failing.Begin();
    failing.Step({false, false, false, false, true});
    Require(failing.state().phase == gengine::Phase::kFaulted && failing.TakeSounds() == 0, "fault must discard sound");
    const auto fault_memory = failing.MemoryCounters();
    Require(std::find_if(fault_memory.begin(), fault_memory.end(),
                         [](const auto& counter) {
                             return counter.name == "vm_pool_live_valid" && counter.value == 0;
                         }) != fault_memory.end(),
            "faulted VM must report unavailable live columns");
    failing.Reset();
    Require(failing.state().phase == gengine::Phase::kStart && failing.state().diagnostic.empty(), "fault reset");

    // 行番号31/32と末尾の接触を反映してから削除し、無敵時間内の多重ダメージを防ぐ。
    auto vm = gvm::Vm(bytes);
    const auto init = vm.ResolveFunction<void(std::uint32_t)>("initialize");
    const auto hits = vm.ResolveFunction<void(std::uint32_t, std::uint32_t, std::uint32_t)>("apply_hits");
    const auto finish = vm.ResolveFunction<void()>("finish_step");
    const auto hp = vm.ResolveFunction<std::int32_t()>("player_hp");
    vm.Call(init, 4u);
    vm.Call(hits, 1u, 0u, 0x80000000u);
    vm.Call(hits, 1u, 32u, 1u);
    vm.Call(hits, 1u, 992u, 0x80000000u);
    vm.Call(finish);
    Require(vm.Call(hp) == 2, "multiple contacts during immunity");
    {
        auto view = vm.InspectPool("threats");
        Require(view.Column<float>("position", 0).size() == 1021, "mask boundary deletion");
    }

    // 双方の致死ダメージを同じ更新へ集約し、失敗を優先する。
    vm.Reset();
    vm.Call(init, 2u);
    const auto step = vm.ResolveFunction<void(Vector, bool, float)>("step");
    const auto result = vm.ResolveFunction<std::uint32_t()>("result");
    vm.Reset();
    for (auto tick = 0; tick < 7; ++tick) {
        vm.Call(step, Vector{0, 0}, true, 1.0f / 60);
        vm.Call(finish);
    }
    {
        auto view = vm.InspectPool("shots");
        Require(view.Column<float>("position", 0).size() == 2, "six tick firing interval");
    }
    vm.Call(step, Vector{0, 0}, false, 4.0f);
    vm.Call(finish);
    {
        auto view = vm.InspectPool("shots");
        Require(view.Column<float>("position", 0).empty(), "expired bullets reclaimed");
    }
    vm.Reset();
    vm.Call(init, 2u);
    for (auto hit = 0; hit < 3; ++hit) {
        vm.Call(step, Vector{0, 0}, hit == 2, 0.5f);
        vm.Call(hits, 1u, 0u, 1u);
        if (hit == 2) {
            vm.Call(hits, 0u, 0u, 1u);
        }
        vm.Call(finish);
    }
    Require(vm.Call(result) == 2 && vm.Call(hp) == 0, "simultaneous defeat priority");

    // 一括更新の末尾と削除後の行対応を、実際のゲームの弾で確かめる。
    for (const auto count : {63u, 64u, 65u}) {
        vm.Reset();
        vm.Call(init, 4u);
        for (auto first = count; first < 1024; first += 32) {
            const auto remaining = std::min(32u, 1024u - first);
            const auto mask = remaining == 32 ? 0xffffffffu : (1u << remaining) - 1u;
            vm.Call(hits, 1u, first, mask);
        }
        vm.Call(finish);
        auto before = std::vector<float>{};
        {
            auto view = vm.InspectPool("threats");
            const auto y = view.Column<float>("position", is_3d ? 2 : 1);
            before.assign(y.begin(), y.end());
        }
        Require(before.size() == count, "compacted row count");
        vm.Call(step, Vector{0, 0}, false, 0.01f);
        {
            auto view = vm.InspectPool("threats");
            const auto y = view.Column<float>("position", is_3d ? 2 : 1);
            for (std::size_t row = 0; row < y.size(); ++row) {
                Require(std::abs(y[row] - (before[row] + (is_3d ? 0.01f : -0.01f))) < 0.00001f, "batch tail movement");
            }
        }
    }

    // 容量超過でも既存の弾を上書きせず、追加失敗だけを数える。
    vm.Reset();
    for (auto repeat = 0; repeat < 5; ++repeat) {
        vm.Call(init, 4u);
    }
    const auto dropped = vm.ResolveFunction<std::uint32_t()>("dropped_shots");
    Require(vm.Call(dropped) == 1024, "pool overflow diagnostic");
    {
        auto view = vm.InspectPool("threats");
        const auto x = view.Column<float>("position", 0);
        Require(x.size() == 4096 && x.front() == -8 && x.back() == 7.75f, "full pool preservation");
    }

    if constexpr (std::is_same_v<Vector, gvm::Vec3>) {
        // XYZすべてを使う距離と相対移動を確認し、平面への投影で誤接触させない。
        Require(gengine::Game::Contact({0, 0, -2}, {0, 0, 2}, {0, 0, 0}, {0, 0, 0}, 0.1f), "Z swept hit");
        Require(gengine::Game::Contact({0, 0, 2}, {0, 0, 2}, {0, 0, 0}, {0, 0, 0}, 0.1f) == false, "Z separated miss");
        Require(gengine::Game::Contact({0, 1, -2}, {0, 1, 2}, {0, 0, 0}, {0, 0, 0}, 0.1f) == false,
                "height separated miss");
        Require(gengine::Game::Contact({0, 0, 2}, {0, 0, -2}, {0, 0, -2}, {0, 0, 2}, 0.1f), "relative Z hit");
        Require(gengine::Game::Contact({0, 0, 0.35f}, {0, 0, 0.35f}, {}, {}, 0.35f), "stationary tangent");
        game.Begin();
        game.Step({false, true, true, false, false, false, true});
        const auto p = game.state().player;
        Require(std::abs(std::sqrt(p.x * p.x + (p.y - 2) * (p.y - 2) + (p.z - 4) * (p.z - 4)) - 5.0f / 60) < 0.00001f,
                "three axis normalized speed");
        for (auto i = 0; i < 300; ++i) {
            game.Step({false, true, false, true, false, false, true});
        }
        Require(game.state().player.x == 8 && game.state().player.y == 6 && game.state().player.z == 6,
                "upper XYZ bounds");
        // 敵弾に当たる前に各下限へ到達する方向を個別に確かめる。
        game.Begin();
        for (auto i = 0; i < 180; ++i) {
            game.Step({true, false, true, false, false, true, false});
        }
        Require(game.state().player.x == -8 && game.state().player.y == 0.5f, "lower XY bounds");
        for (auto i = 0; i < 60; ++i) {
            game.Step({false, false, true, false, false});
        }
        Require(game.state().player.z == -6, "lower Z bound");

        // 実弾が三成分で自機を狙い、速度12を保つことを列から確認する。
        vm.Reset();
        vm.Call(step, Vector{1, 1, 0}, false, 0.5f);
        {
            auto view = vm.InspectPool("threats");
            const auto vx = view.Column<float>("velocity", 0)[0];
            const auto vy = view.Column<float>("velocity", 1)[0];
            const auto vz = view.Column<float>("velocity", 2)[0];
            Require(vx > 0 && vy > 0 && vz > 0, "aim uses XYZ");
            Require(std::abs(std::sqrt(vx * vx + vy * vy + vz * vz) - 12) < 0.00001f, "aim speed");
            Require(std::abs(vx / vz - 2.5f / 8) < 0.00001f && std::abs(vy / vz - 2.5f / 8) < 0.00001f,
                    "aim direction");
        }
    }
    // 次元選択の誤りは初期化時に拒否し、実行を開始しない。
    auto mismatch_rejected = false;
    try {
        auto mismatch = gengine::Game(bytes, 1000000, is_3d ? gengine::GameMode::k2d : gengine::GameMode::k3d);
    } catch (const gvm::Error&) {
        mismatch_rejected = true;
    }
    Require(mismatch_rejected, "mismatched game signature must fail");

    // 通常負荷と1024発の負荷を分け、VM更新・接触・結果転送を記録する。
    for (const auto preset : {1u, 4u}) {
        auto updates = std::vector<std::uint64_t>{};
        auto contacts = std::vector<std::uint64_t>{};
        auto applies = std::vector<std::uint64_t>{};
        for (auto run = 0; run < 100; ++run) {
            game.Reset(preset);
            for (auto tick = 0; tick < 10; ++tick) {
                game.Step({false, true, false, false, true});
                updates.push_back(game.state().update_ns);
                contacts.push_back(game.state().contact_ns);
                applies.push_back(game.state().apply_ns);
            }
        }
        std::sort(updates.begin(), updates.end());
        std::sort(contacts.begin(), contacts.end());
        std::sort(applies.begin(), applies.end());
        std::cout << "preset=" << preset << " update_p95_ns=" << updates[950] << " contact_p95_ns=" << contacts[950]
                  << " apply_p95_ns=" << applies[950] << " vm_owned_bytes=" << game.state().owned_bytes
                  << " vm_peak_bytes=" << game.state().peak_owned_bytes << '\n';
    }
    std::cout << "game rules, reset, fixed update, collision and presets passed\n";
}

int main(int argc, char** argv)
{
    try {
        Require(argc == 2 || argc == 3, "module and optional 3d argument required");
        auto stream = std::ifstream(argv[1], std::ios::binary);
        const auto bytes = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), {});
        if (argc == 3) {
            CheckGame<gvm::Vec3>(bytes, gengine::GameMode::k3d);
        }
        else {
            CheckGame<gvm::Vec2>(bytes, gengine::GameMode::k2d);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
