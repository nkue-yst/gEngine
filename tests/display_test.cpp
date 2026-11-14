#include "window_display.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace
{
    void Require(bool value, const char* message)
    {
        if (value == false) {
            throw std::runtime_error(message);
        }
    }
    void Pump()
    {
        auto message = MSG{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    void Draw(HWND window, rengine::Renderer& renderer)
    {
        Pump();
        auto rect = RECT{};
        GetClientRect(window, &rect);
        Require(renderer.Resize(rect.right, rect.bottom).has_value(), "resize failed");
        auto pass = rengine::DefaultPass();
        pass.viewport[2] = float(rect.right);
        pass.viewport[3] = float(rect.bottom);
        pass.clear_color[1] = .1f;
        Require(renderer.Render({&pass, 1, 1}).has_value(), "render after display change failed");
        Require(renderer.WaitIdle().has_value() && renderer.Stats()->validation_errors == 0,
                "display validation error");
    }
    struct Window
    {
        HWND handle;
        Window()
        {
            auto type = WNDCLASSW{};
            type.lpfnWndProc = DefWindowProcW;
            type.hInstance = GetModuleHandleW(nullptr);
            type.lpszClassName = L"gEngineDisplayTest";
            RegisterClassW(&type);
            handle =
                CreateWindowW(type.lpszClassName, L"gEngine display verification", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50,
                              50, 960, 580, nullptr, nullptr, type.hInstance, nullptr);
            Require(handle != nullptr, "display test window unavailable");
        }
        ~Window()
        {
            DestroyWindow(handle);
        }
    };
} // namespace
int main(int argc, char** argv)
{
    try {
        Require(argc == 2, "backend directory required");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        auto window = Window{};
        auto settings = rengine::Settings{};
        settings.window = window.handle;
        settings.backend = "d3d12";
        settings.backend_directory = argv[1];
        settings.debug = true;
        auto result = rengine::Renderer::Create(settings);
        if (result.has_value() == false) {
            throw std::runtime_error(result.error().message);
        }
        auto renderer = std::move(*result);
        auto display = gengine::WindowDisplay(window.handle, *renderer, true);
        display.Restore({});
        const auto original = display.confirmed();
        Require(display.enabled() && display.failed() == false, "initial display failed");
        auto client = RECT{};
        GetClientRect(window.handle, &client);
        Require(client.right == 1920 && client.bottom == 1080, "initial client is not FHD");

        // PIEの禁止、下書きの隔離、手動取消しと確定を実際のウィンドウで確認する。
        auto embedded = gengine::WindowDisplay(window.handle, *renderer, false);
        embedded.Cycle(0, 1);
        Require(embedded.Apply() == false && embedded.confirmed().mode == gengine::DisplayMode::kWindowed,
                "PIE changed display");
        for (auto index = 0u; index < gengine::kGameResolutions.size(); ++index) {
            Require(display.Apply() && display.pending() && display.confirmed() == original,
                    "draft modified confirmed settings");
            Draw(window.handle, *renderer);
            GetClientRect(window.handle, &client);
            Require(client.right == static_cast<LONG>(display.draft().resolution.width) &&
                        client.bottom == static_cast<LONG>(display.draft().resolution.height),
                    "window client resolution differs");
            display.Cancel();
            Require(display.pending() == false && display.confirmed() == original, "cancel did not restore settings");
            for (auto step = 0u; step <= index; ++step) {
                display.Cycle(1, 1);
            }
        }
        display.Cancel();
        display.Cycle(0, 1);
        Require(display.Apply(), "borderless apply failed");
        Draw(window.handle, *renderer);
        auto bounds = RECT{};
        GetWindowRect(window.handle, &bounds);
        const auto monitor = display.monitors().front().bounds;
        Require(EqualRect(&bounds, &monitor) && (GetWindowLongPtrW(window.handle, GWL_STYLE) & WS_CAPTION) == 0,
                "borderless did not cover the monitor");
        display.Keep();
        Require(display.confirmed().mode == gengine::DisplayMode::kBorderless && display.pending() == false,
                "confirmation failed");
        display.Restore(original);

        // 縦横比を変えたウィンドウから適用しても、期限切れでは元の配置へ戻る。
        SetWindowPos(window.handle, nullptr, 100, 100, 900, 650, SWP_NOZORDER | SWP_NOACTIVATE);
        auto before = RECT{};
        GetWindowRect(window.handle, &before);
        display.Cycle(0, 1);
        Require(display.Apply(), "timeout candidate failed");
        const auto deadline = GetTickCount64() + 15100;
        while (GetTickCount64() < deadline) {
            Pump();
            display.Poll();
            Sleep(8);
        }
        display.Keep();
        GetWindowRect(window.handle, &bounds);
        Require(display.pending() == false && display.confirmed() == original && EqualRect(&bounds, &before),
                "timeout did not restore prior window");
        std::cout << "FHD/WQHD/4K window, borderless, confirm, cancel, 15-second timeout and PIE isolation passed"
                  << std::endl;

        // 対応モードだけを全画面で通す。非対応4Kは検証済みとしない。
        for (auto index = 0u; index < gengine::kGameResolutions.size(); ++index) {
            display.Restore(original);
            display.Cycle(0, -1);
            for (auto step = 0u; step < index; ++step) {
                display.Cycle(1, 1);
            }
            const auto& modes = display.monitors().front().modes;
            const auto resolution = gengine::kGameResolutions[index];
            if (std::find(modes.begin(), modes.end(), resolution) == modes.end()) {
                Require(display.Apply() == false && display.failed() && display.confirmed() == original,
                        "unsupported mode changed settings");
                std::cout << "unsupported fullscreen rejected: " << resolution.width << 'x' << resolution.height
                          << std::endl;
                continue;
            }
            SetForegroundWindow(window.handle);
            std::cout << "waiting for foreground fullscreen " << resolution.width << 'x' << resolution.height
                      << std::endl;
            const auto limit = GetTickCount64() + 60000;
            while (GetForegroundWindow() != window.handle && GetTickCount64() < limit) {
                Pump();
                Sleep(20);
            }
            Require(GetForegroundWindow() == window.handle, "fullscreen requires foreground test window");
            Require(display.Apply() && *renderer->Fullscreen(), "fullscreen apply failed");
            Draw(window.handle, *renderer);
            display.Keep();
            // OSによる全画面解除を同じ経路で扱い、次の操作を受けられる状態に戻す。
            Require(renderer->SetFullscreen(false, 1920, 1080).has_value(), "external fullscreen exit failed");
            display.Poll();
            Require(display.confirmed().mode == gengine::DisplayMode::kWindowed && display.failed(),
                    "external display exit was not recovered");
            Draw(window.handle, *renderer);
        }
        auto disconnected = original;
        disconnected.monitor = "missing-display";
        display.Restore(disconnected);
        Require(display.failed() && display.confirmed().mode == gengine::DisplayMode::kWindowed,
                "missing monitor not recovered");
        std::cout
            << "display settings: fullscreen, external exit, unavailable mode and missing monitor recovery passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
