#include "window_display.hpp"
#include <algorithm>
#include <stdexcept>

namespace gengine
{
    namespace
    {
        void Check(rengine::Result result)
        {
            if (result.has_value() == false) {
                throw std::runtime_error(result.error().message);
            }
        }
        BOOL CALLBACK Collect(HMONITOR monitor, HDC, LPRECT, LPARAM data)
        {
            auto info = MONITORINFOEXW{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) == FALSE) {
                return TRUE;
            }
            auto value = DisplayMonitor{};
            for (const auto c : info.szDevice) {
                if (c == 0) {
                    break;
                }
                value.id.push_back(static_cast<char>(c));
            }
            value.bounds = info.rcMonitor;
            value.work = info.rcWork;
            auto mode = DEVMODEW{};
            mode.dmSize = sizeof(mode);
            for (auto index = DWORD{0}; EnumDisplaySettingsW(info.szDevice, index, &mode) != FALSE; ++index) {
                const auto resolution = Resolution{mode.dmPelsWidth, mode.dmPelsHeight};
                if (std::find(kGameResolutions.begin(), kGameResolutions.end(), resolution) != kGameResolutions.end() &&
                    std::find(value.modes.begin(), value.modes.end(), resolution) == value.modes.end()) {
                    value.modes.push_back(resolution);
                }
            }
            std::sort(value.modes.begin(), value.modes.end(), [](const auto& a, const auto& b) {
                return a.width < b.width;
            });
            static_cast<std::vector<DisplayMonitor>*>(reinterpret_cast<void*>(data))->push_back(std::move(value));
            return TRUE;
        }
    } // namespace

    WindowDisplay::WindowDisplay(HWND window, rengine::Renderer& renderer, bool enabled)
        : window_(window), renderer_(renderer), enabled_(enabled && renderer.HasPresentation())
    {
        RefreshMonitors();
    }
    bool WindowDisplay::enabled() const
    {
        return enabled_;
    }
    bool WindowDisplay::changing() const
    {
        return changing_;
    }
    bool WindowDisplay::failed() const
    {
        return failed_;
    }
    bool WindowDisplay::pending() const
    {
        return pending_;
    }
    const DisplaySettings& WindowDisplay::confirmed() const
    {
        return confirmed_;
    }
    const DisplaySettings& WindowDisplay::current() const
    {
        return current_;
    }
    const DisplaySettings& WindowDisplay::draft() const
    {
        return draft_;
    }
    const std::vector<DisplayMonitor>& WindowDisplay::monitors() const
    {
        return monitors_;
    }
    std::uint32_t WindowDisplay::seconds() const
    {
        if (pending_ == false) {
            return 0;
        }
        const auto remaining =
            std::chrono::ceil<std::chrono::seconds>(deadline_ - std::chrono::steady_clock::now()).count();
        return static_cast<std::uint32_t>(std::max(std::int64_t{0}, remaining));
    }
    void WindowDisplay::RefreshMonitors()
    {
        if (changing_) {
            return;
        }
        monitors_.clear();
        EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&monitors_));
    }
    void WindowDisplay::Cycle(std::uint32_t field, int direction)
    {
        if (enabled_ == false || pending_ || monitors_.empty()) {
            return;
        }
        const auto next = [direction](std::size_t index, std::size_t count) {
            return (index + (direction < 0 ? count - 1 : 1)) % count;
        };
        if (field == 0) {
            draft_.mode = static_cast<DisplayMode>(next(static_cast<std::size_t>(draft_.mode), 3));
        }
        else if (field == 1 && draft_.mode != DisplayMode::kBorderless) {
            const auto found = std::find(kGameResolutions.begin(), kGameResolutions.end(), draft_.resolution);
            draft_.resolution = kGameResolutions[next(static_cast<std::size_t>(found - kGameResolutions.begin()),
                                                      kGameResolutions.size())];
        }
        else if (field == 2) {
            const auto found = std::find_if(monitors_.begin(), monitors_.end(), [&](const auto& monitor) {
                return monitor.id == draft_.monitor;
            });
            const auto index = found == monitors_.end() ? 0u : static_cast<std::size_t>(found - monitors_.begin());
            draft_.monitor = monitors_[next(index, monitors_.size())].id;
        }
        else if (field == 3) {
            draft_.vsync = draft_.vsync == false;
        }
        failed_ = false;
    }
    void WindowDisplay::Set(const DisplaySettings& settings)
    {
        if (enabled_ == false || ValidDisplaySettings(settings) == false || monitors_.empty()) {
            throw std::runtime_error("display settings unavailable");
        }
        auto found = std::find_if(monitors_.begin(), monitors_.end(), [&](const auto& monitor) {
            return monitor.id == settings.monitor;
        });
        if (settings.monitor.empty()) {
            found = monitors_.begin();
        }
        if (found == monitors_.end()) {
            throw std::runtime_error("display monitor disconnected");
        }
        // 未対応の全画面解像度は、ウィンドウの状態を変更する前に拒否する。
        if (settings.mode == DisplayMode::kFullscreen &&
            std::find(found->modes.begin(), found->modes.end(), settings.resolution) == found->modes.end()) {
            throw std::runtime_error("fullscreen resolution unavailable");
        }
        auto monitor = *found;
        changing_ = true;
        try {
            Check(renderer_.SetFullscreen(false, settings.resolution.width, settings.resolution.height));
            // 全画面解除でデスクトップ寸法が戻るため、配置先の矩形を取り直す。
            monitors_.clear();
            EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&monitors_));
            found = std::find_if(monitors_.begin(), monitors_.end(), [&](const auto& item) {
                return item.id == monitor.id;
            });
            if (found == monitors_.end()) {
                throw std::runtime_error("display monitor disconnected during transition");
            }
            monitor = *found;
            ShowWindow(window_, SW_RESTORE);
            const auto style = settings.mode == DisplayMode::kWindowed ? WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN
                                                                       : WS_POPUP | WS_CLIPCHILDREN;
            SetLastError(0);
            if (SetWindowLongPtrW(window_, GWL_STYLE, style | WS_VISIBLE) == 0 && GetLastError() != 0) {
                throw std::runtime_error("cannot change window style");
            }
            auto rect = monitor.bounds;
            if (settings.mode == DisplayMode::kWindowed) {
                rect = {0, 0, static_cast<LONG>(settings.resolution.width),
                        static_cast<LONG>(settings.resolution.height)};
                AdjustWindowRectExForDpi(&rect, style, FALSE, 0, GetDpiForWindow(window_));
                const auto width = rect.right - rect.left;
                const auto height = rect.bottom - rect.top;
                // 小さいデスクトップでもタイトルバーを画面内に残す。描画面は指定したピクセル数を保つ。
                const auto x = monitor.work.left + std::max(0L, (monitor.work.right - monitor.work.left - width) / 2);
                const auto y = monitor.work.top + std::max(0L, (monitor.work.bottom - monitor.work.top - height) / 2);
                rect = {x, y, x + width, y + height};
            }
            if (SetWindowPos(window_, HWND_NOTOPMOST, rect.left, rect.top, rect.right - rect.left,
                             rect.bottom - rect.top, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW) == FALSE) {
                throw std::runtime_error("cannot position display window");
            }
            if (settings.mode == DisplayMode::kFullscreen) {
                Check(renderer_.SetFullscreen(true, settings.resolution.width, settings.resolution.height));
            }
            Check(renderer_.SetVSync(settings.vsync));
            current_ = settings;
            current_.monitor = monitor.id;
            changing_ = false;
        } catch (...) {
            changing_ = false;
            throw;
        }
    }
    bool WindowDisplay::Apply()
    {
        EndTemporary();
        if (enabled_ == false || pending_) {
            return false;
        }
        try {
            RefreshMonitors();
            previous_.length = sizeof(previous_);
            restore_placement_ =
                confirmed_.mode == DisplayMode::kWindowed && GetWindowPlacement(window_, &previous_) != FALSE;
            Set(draft_);
            draft_ = current_;
            pending_ = true;
            failed_ = false;
            deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            return true;
        } catch (const std::exception&) {
            failed_ = true;
            Revert();
            return false;
        }
    }
    void WindowDisplay::Keep()
    {
        if (pending_ == false) {
            return;
        }
        // キーイベントがタイマーより先に届いても、期限切れを確定させない。
        if (seconds() == 0) {
            Cancel();
            return;
        }
        confirmed_ = current_;
        pending_ = false;
        restore_placement_ = false;
    }
    void WindowDisplay::SafeWindow()
    {
        auto safe = DisplaySettings{};
        safe.monitor = monitors_.empty() ? "" : monitors_.front().id;
        Set(safe);
        confirmed_ = current_;
    }
    void WindowDisplay::Revert()
    {
        pending_ = false;
        try {
            Set(confirmed_);
            if (restore_placement_) {
                SetWindowPlacement(window_, &previous_);
            }
        } catch (const std::exception&) {
            SafeWindow();
            failed_ = true;
        }
        restore_placement_ = false;
        draft_ = confirmed_;
    }
    void WindowDisplay::Cancel()
    {
        if (pending_) {
            Revert();
        }
        draft_ = confirmed_;
    }
    void WindowDisplay::Restore(const DisplaySettings& settings)
    {
        restore_fullscreen_on_focus_ = false;
        if (enabled_ == false) {
            return;
        }
        failed_ = false;
        pending_ = false;
        restore_placement_ = false;
        try {
            Set(settings);
            confirmed_ = current_;
        } catch (const std::exception&) {
            SafeWindow();
            failed_ = true;
        }
        draft_ = confirmed_;
    }
    void WindowDisplay::Poll()
    {
        if (enabled_ == false || changing_ || monitors_.empty()) {
            return;
        }
        // 外部アプリへ移ったPauseでは全画面を奪い返さず、保存済みモードを保って復帰を待つ。
        if (restore_fullscreen_on_focus_ && GetForegroundWindow() == window_ && IsIconic(window_) == FALSE) {
            restore_fullscreen_on_focus_ = false;
            try {
                Set(confirmed_);
            } catch (const std::exception&) {
                SafeWindow();
                failed_ = true;
            }
        }
        if (pending_ && seconds() == 0) {
            Cancel();
        }
        if (current_.mode == DisplayMode::kFullscreen) {
            const auto fullscreen = renderer_.Fullscreen();
            if (fullscreen.has_value() && *fullscreen == false) {
                pending_ = false;
                SafeWindow();
                draft_ = confirmed_;
                failed_ = true;
            }
        }
        if (std::none_of(monitors_.begin(), monitors_.end(), [&](const auto& monitor) {
                return monitor.id == current_.monitor;
            })) {
            pending_ = false;
            SafeWindow();
            draft_ = confirmed_;
            failed_ = true;
        }
        else if (current_.mode == DisplayMode::kBorderless && IsIconic(window_) == FALSE) {
            const auto monitor = std::find_if(monitors_.begin(), monitors_.end(), [&](const auto& item) {
                return item.id == current_.monitor;
            });
            auto bounds = RECT{};
            GetWindowRect(window_, &bounds);
            // デスクトップの解像度変更後も、ボーダーレスをモニター全体へ合わせる。
            if (EqualRect(&bounds, &monitor->bounds) == FALSE) {
                Set(current_);
            }
        }
    }

    void WindowDisplay::BeginTemporary()
    {
        if (temporary_ || enabled_ == false) {
            return;
        }
        Cancel();
        restore_fullscreen_on_focus_ = false;
        temporary_placement_.length = sizeof(temporary_placement_);
        if (GetWindowPlacement(window_, &temporary_placement_) == FALSE) {
            throw std::runtime_error("cannot retain game window placement");
        }
        auto windowed = confirmed_;
        windowed.mode = DisplayMode::kWindowed;
        Set(windowed);
        temporary_ = true;
    }

    void WindowDisplay::EndTemporary()
    {
        if (temporary_ == false) {
            return;
        }
        temporary_ = false;
        const auto minimized = IsIconic(window_) != FALSE;
        if (confirmed_.mode == DisplayMode::kFullscreen && (GetForegroundWindow() != window_ || minimized)) {
            restore_fullscreen_on_focus_ = true;
            return;
        }
        try {
            Set(confirmed_);
            if (confirmed_.mode == DisplayMode::kWindowed) {
                SetWindowPlacement(window_, &temporary_placement_);
            }
            if (minimized) {
                ShowWindow(window_, SW_MINIMIZE);
            }
        } catch (const std::exception&) {
            SafeWindow();
            failed_ = true;
        }
    }
} // namespace gengine
