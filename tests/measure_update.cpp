// gLangのC0と同じ列・演算順・数値検査で、手書きC++と組込みVMを比較する。
#include "performance.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gvm/gvm.hpp>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
    using Clock = std::chrono::steady_clock;
    float Checked(float value)
    {
        if (std::isfinite(value) == false) {
            throw std::runtime_error("non-finite arithmetic");
        }
        return value;
    }

    struct Native
    {
        std::vector<std::vector<float>> position;
        std::vector<std::vector<float>> velocity;
        std::vector<float> radius;
        std::vector<float> life;

        Native(std::size_t dimensions, std::size_t count)
            : position(dimensions, std::vector<float>(count)), velocity(position), radius(count), life(count)
        {
        }
        void Fill()
        {
            for (auto i = std::size_t{0}; i < life.size(); ++i) {
                for (auto d = std::size_t{0}; d < position.size(); ++d) {
                    position[d][i] = std::array{float(i), 0.0f, 1.0f}[d];
                    velocity[d][i] = std::array{2.0f, 1.0f, -1.0f}[d];
                }
                radius[i] = 0.5f;
                life[i] = float(i % 4) + 1.0f;
            }
        }
        void Update(float dt)
        {
            // 有限値検査と削除・短絡の条件も比較対象へ含める。C0では削除は発生しない。
            for (auto i = std::size_t{0}; i < life.size(); ++i) {
                for (auto d = std::size_t{0}; d < position.size(); ++d) {
                    position[d][i] = Checked(position[d][i] + Checked(velocity[d][i] * dt));
                }
                life[i] = Checked(life[i] - dt);
                if (life[i] <= 0) {
                    throw std::runtime_error("C0 fixture unexpectedly removed an item");
                }
                if (position[1][i] != 0 && Checked(radius[i] / position[1][i]) > 1) {
                    radius[i] = 0.25f;
                }
            }
        }
        void Compare(gvm::Vm& vm) const
        {
            const auto view = vm.InspectPool("bullets");
            const auto compare = [&](const char* field, std::uint32_t component, const auto& expected) {
                const auto column = view.Column<float>(field, component);
                if (column.size() != expected.size()) {
                    throw std::runtime_error("live count differs");
                }
                for (auto i = std::size_t{0}; i < column.size(); ++i) {
                    if (std::bit_cast<std::uint32_t>(column[i]) != std::bit_cast<std::uint32_t>(expected[i])) {
                        throw std::runtime_error(std::string("column differs: ") + field);
                    }
                }
            };
            for (auto d = std::uint32_t{0}; d < position.size(); ++d) {
                compare("position", d, position[d]);
                compare("velocity", d, velocity[d]);
            }
            compare("radius", 0, radius);
            compare("life", 0, life);
        }
    };
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 3 || argc > 4 || (std::string(argv[2]) != "2d" && std::string(argv[2]) != "3d")) {
            throw std::invalid_argument("module 2d|3d [smoke]");
        }
        auto file = std::ifstream(std::filesystem::path(argv[1]), std::ios::binary);
        const auto bytes = std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), {}};
        auto hash = std::uint64_t{0xcbf29ce484222325};
        for (const auto value : bytes) {
            hash = (hash ^ value) * 0x100000001b3;
        }
        const auto samples = argc == 4 ? 2u : 100u;
        const auto warmup = argc == 4 ? 1u : 30u;
        const auto dimensions = std::string(argv[2]) == "3d" ? 3u : 2u;
        for (const auto count : {1000u, 10000u, 100000u}) {
            const auto prepared = Clock::now();
            auto vm = gvm::Vm(bytes);
            const auto prepare_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - prepared).count();
            const auto fill = vm.ResolveFunction<bool(std::uint32_t)>("fill");
            const auto step = vm.ResolveFunction<void(float)>("step");
            auto native = Native(dimensions, count);
            auto native_times = std::vector<std::uint64_t>{};
            auto vm_times = std::vector<std::uint64_t>{};
            native_times.reserve(samples);
            vm_times.reserve(samples);
            // 標本ごとに同じ状態へ戻し、生成と全列照合を更新の測定区間から外す。
            for (auto i = 0u; i < warmup + samples; ++i) {
                native.Fill();
                if (vm.CallWithFuel(100000000, fill, count) == false) {
                    throw std::runtime_error("fill failed");
                }
                const auto begin = Clock::now();
                native.Update(0.001f);
                const auto middle = Clock::now();
                vm.CallWithFuel(100000000, step, 0.001f);
                const auto end = Clock::now();
                native.Compare(vm);
                if (i >= warmup) {
                    native_times.push_back(static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(middle - begin).count()));
                    vm_times.push_back(static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(end - middle).count()));
                }
            }
            auto stats = GvmStats{};
            auto memory = GvmMemoryStats{};
            if (gvm_get_stats(vm.native_handle(), &stats) != GVM_OK ||
                gvm_get_memory_stats(vm.native_handle(), &memory) != GVM_OK) {
                throw std::runtime_error("memory query failed");
            }
            for (const auto native_path : {true, false}) {
                const auto summary = gengine::Summarize(native_path ? "cpp-native" : "cpp-width64",
                                                        native_path ? native_times : vm_times);
                std::cout << nlohmann::json{{"path", summary.name},
                                            {"game", argv[2]},
                                            {"count", count},
                                            {"samples", samples},
                                            {"warm", warmup},
                                            {"mean_ns", summary.mean_ns},
                                            {"median_ns", summary.p50_ns},
                                            {"p95_ns", summary.p95_ns},
                                            {"p99_ns", summary.p99_ns},
                                            {"bytecode_hash", hash},
                                            {"prepare_ns", prepare_ns},
                                            {"owned", stats.owned_bytes},
                                            {"pool_reserved", memory.pool_data_bytes},
                                            {"live", count},
                                            {"all_columns_bit_equal", true}}
                                 .dump()
                          << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
