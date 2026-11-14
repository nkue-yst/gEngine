#include "play_window.hpp"
#include "product_entry.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <shellapi.h>

namespace
{
    bool IsProductEntry()
    {
        const auto module = GetModuleHandleW(nullptr);
        const auto entry = FindResourceW(module, MAKEINTRESOURCEW(gengine::kProductMarkerId), RT_RCDATA);
        if (entry == nullptr) {
            return false;
        }
        const auto loaded = LoadResource(module, entry);
        const auto* bytes = loaded != nullptr ? static_cast<const char*>(LockResource(loaded)) : nullptr;
        if (bytes == nullptr || SizeofResource(module, entry) != gengine::kProductMarker.size() ||
            std::string_view(bytes, gengine::kProductMarker.size()) != gengine::kProductMarker) {
            throw std::runtime_error("invalid product entry resource");
        }
        return true;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // フォントと描画面を物理ピクセルに合わせ、モニター間のDPI変更をウィンドウ側で受ける。
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto managed = false;
    try {
        auto path = std::array<wchar_t, 32768>{};
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        const auto directory = std::filesystem::path(path.data()).parent_path();
        // 通常配布の入口ではゲームだけを選び、開発用プリセットを適用しない。
        auto count = 0;
        auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (arguments == nullptr) {
            throw std::runtime_error("cannot read launch arguments");
        }
        auto options = std::map<std::wstring, std::wstring>{};
        for (auto index = 1; index + 1 < count; index += 2) {
            if (options.emplace(arguments[index], arguments[index + 1]).second == false) {
                LocalFree(arguments);
                throw std::invalid_argument("duplicate launch option");
            }
        }
        LocalFree(arguments);
        // Editor管理下では起動失敗を標準エラーと終了コードで返し、ダイアログ待ちを残さない。
        if (options.contains(L"--managed")) {
            managed = options.at(L"--managed") == L"1";
            if (managed == false) {
                throw std::invalid_argument("--managed requires 1");
            }
            options.erase(L"--managed");
            count -= 2;
        }
        // 通常起動だけが利用者の記録を保存する。試験先と保存しない起動を明示できる。
        auto profile_storage = gengine::ProfileStorageOptions{true, {}};
        if (options.contains(L"--ephemeral")) {
            if (options.at(L"--ephemeral") != L"1" || options.contains(L"--user-data")) {
                throw std::invalid_argument("--ephemeral requires 1 and cannot combine with --user-data");
            }
            profile_storage.persistent = false;
            options.erase(L"--ephemeral");
            count -= 2;
        }
        if (options.contains(L"--user-data")) {
            profile_storage.directory = std::filesystem::absolute(options.at(L"--user-data"));
            options.erase(L"--user-data");
            count -= 2;
        }
        // ソースからの試遊だけに限定する。製品EXEと通常保存では局所開始を受け付けない。
        auto preview_preset = std::optional<std::uint32_t>{};
        if (options.contains(L"--preview-preset")) {
            const auto value = options.at(L"--preview-preset");
            if (IsProductEntry() || profile_storage.persistent || options.contains(L"--project") == false ||
                options.contains(L"--scene") == false || options.contains(L"--module") == false || value.empty() ||
                value.size() > 2 || value.find_first_not_of(L"0123456789") != std::wstring::npos ||
                std::stoul(value) > 31) {
                throw std::invalid_argument(
                    "preview preset requires source project, ephemeral mode and developer Runner");
            }
            preview_preset = static_cast<std::uint32_t>(std::stoul(value));
            options.erase(L"--preview-preset");
            count -= 2;
        }
        auto metrics_path = std::optional<std::filesystem::path>{};
        auto capture_config = gengine::PerformanceConfig{};
        if (options.contains(L"--metrics")) {
            metrics_path = std::filesystem::absolute(options.at(L"--metrics"));
            options.erase(L"--metrics");
            count -= 2;
            if (std::filesystem::exists(*metrics_path) ||
                std::filesystem::is_directory(metrics_path->parent_path()) == false) {
                throw std::invalid_argument("metrics requires a new file in an existing directory");
            }
            for (const auto* option : {L"--warmup", L"--samples"}) {
                if (options.contains(option)) {
                    const auto& value = options.at(option);
                    if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos ||
                        value.size() > 4) {
                        throw std::invalid_argument("invalid capture count");
                    }
                    (std::wstring_view(option) == L"--warmup" ? capture_config.warmup : capture_config.samples) =
                        std::stoul(value);
                    options.erase(option);
                    count -= 2;
                }
            }
            if (capture_config.warmup > 600 || capture_config.samples == 0 || capture_config.samples > 3600) {
                throw std::invalid_argument("warmup 0..600; samples 1..3600");
            }
        }
        const auto requested_mode =
            options.contains(L"--game") ? std::optional<bool>{options.at(L"--game") == L"3d"} : std::nullopt;
        auto source = std::optional<gengine::AssetSource>{};
        auto input = gengine::GameInput{};
        auto references = std::optional<gengine::AssetSource>{};
        if (count == 7 && options.contains(L"--project") && options.contains(L"--scene") &&
            options.contains(L"--module")) {
            const auto encoded = std::filesystem::path(options.at(L"--scene")).generic_u8string();
            input.scene = gengine::LoadProjectScene(
                options.at(L"--project"), std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
            input.module = gengine::ReadModule(options.at(L"--module"));
            source.emplace(directory / "content");
            references.emplace(std::filesystem::absolute(options.at(L"--project")).parent_path());
        }
        else {
            // 保存場所だけを選び、以後は通常ファイル・packで同じゲーム初期化を通す。
            const auto valid_options = std::all_of(options.begin(), options.end(), [](const auto& item) {
                return item.first == L"--game" || item.first == L"--assets" || item.first == L"--pack";
            });
            if (count % 2 != 1 || valid_options == false ||
                (options.contains(L"--assets") && options.contains(L"--pack")) ||
                (options.contains(L"--game") && options.at(L"--game") != L"2d" && options.at(L"--game") != L"3d")) {
                throw std::invalid_argument(
                    "Usage: gengine_runner.exe [--game 2d|3d] [--assets directory | --pack file] "
                    "OR --project project.json --scene scene.json --module game.glbc");
            }
            // 製品EXEの既定入口は自身の隣に固定し、cwdやサンプルの名前には依存させない。
            const auto product_default =
                IsProductEntry() && options.contains(L"--pack") == false && options.contains(L"--assets") == false;
            const auto packed = options.contains(L"--pack") || product_default;
            source.emplace(product_default                 ? directory / "game.bpack"
                           : packed                        ? std::filesystem::path(options.at(L"--pack"))
                           : options.contains(L"--assets") ? std::filesystem::path(options.at(L"--assets"))
                                                           : directory / "content",
                           packed);
            input = gengine::LoadGameInput(*source, requested_mode);
            if (product_default && input.title.empty()) {
                throw std::runtime_error("product pack requires game.json");
            }
        }
        auto window =
            gengine::PlayWindow(nullptr, input.module, directory / "backends",
                                input.scene.game == "3d" ? gengine::GameMode::k3d : gengine::GameMode::k2d, input.scene,
                                false, &*source, references ? &*references : &*source, profile_storage);
        if (preview_preset) {
            window.Reset(*preview_preset);
        }
        if (input.title.empty() == false) {
            // game.jsonの検査済みUTF-8を、Windowsのウィンドウ名へ渡す。
            auto title = std::wstring(
                static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.title.data(),
                                                             static_cast<int>(input.title.size()), nullptr, 0)),
                L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.title.data(), static_cast<int>(input.title.size()),
                                title.data(), static_cast<int>(title.size()));
            if (SetWindowTextW(window.window(), title.c_str()) == FALSE) {
                throw std::runtime_error("cannot set product window title");
            }
        }
        // VMと各部品への転送後は、モジュール原本とpack索引を常駐させない。
        std::vector<std::uint8_t>{}.swap(input.module);
        source.reset();
        references.reset();
        if (metrics_path) {
            window.StartPerformance(capture_config);
        }
        // 計測は通常の入力とメッセージ処理を観測する。完了してもゲームを進めたり終了したりしない。
        const auto save_metrics = [&] {
            const auto text = gengine::PerformanceJson(window.Performance());
            const auto file = CreateFileW(metrics_path->c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                throw std::runtime_error("cannot create metrics report");
            }
            auto written = DWORD{};
            const auto success = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            CloseHandle(file);
            if (success == FALSE || written != text.size()) {
                throw std::runtime_error("cannot write metrics report");
            }
            metrics_path.reset();
        };
        auto message = MSG{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (metrics_path && window.PerformanceActive() == false) {
                save_metrics();
            }
            if (managed && window.game().state().phase == gengine::Phase::kFaulted) {
                throw std::runtime_error(window.game().state().diagnostic);
            }
        }
        if (metrics_path) {
            window.StopPerformance();
            save_metrics();
        }
        return 0;
    } catch (const std::exception& error) {
        if (managed) {
            std::cerr << error.what() << '\n';
        }
        else {
            MessageBoxA(nullptr, error.what(), "gEngine startup failure", MB_ICONERROR);
        }
        return 1;
    }
}
