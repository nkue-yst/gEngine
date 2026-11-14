#pragma once
#include "display_settings.hpp"
#include <chrono>
#include <rengine/renderer.hpp>
#include <vector>
#include <windows.h>

namespace gengine
{
    struct DisplayMonitor
    {
        std::string id;
        std::vector<Resolution> modes;
        RECT bounds{};
        RECT work{};
    };
    // ウィンドウの所有者が使う表示設定。PIEでは無効にし、未確定の設定を保存へ渡さない。
    class WindowDisplay final
    {
      public:
        WindowDisplay(HWND window, rengine::Renderer& renderer, bool enabled);
        bool enabled() const;
        bool changing() const;
        bool failed() const;
        bool pending() const;
        std::uint32_t seconds() const;
        const DisplaySettings& confirmed() const;
        const DisplaySettings& current() const;
        const DisplaySettings& draft() const;
        const std::vector<DisplayMonitor>& monitors() const;
        // 0:モード、1:解像度、2:モニター、3:垂直同期。変更はApplyまで表示へ反映しない。
        void Cycle(std::uint32_t field, int direction);
        bool Apply();
        void Keep();
        void Cancel();
        // 描画前に期限とOSの表示状態を確認し、必要なら使える設定へ戻す。
        void Poll();
        void Restore(const DisplaySettings& settings);
        // WM_DISPLAYCHANGEを受けたら一覧を更新し、次のPollで復帰を判断する。
        void RefreshMonitors();
        // ステージによる一時配置はconfirmed/draftを変えず、終了時に表示モードと位置を戻す。
        void BeginTemporary();
        void EndTemporary();

      private:
        void Set(const DisplaySettings& settings);
        void Revert();
        void SafeWindow();
        HWND window_;
        rengine::Renderer& renderer_;
        bool enabled_;
        bool changing_ = false;
        bool failed_ = false;
        bool pending_ = false;
        bool temporary_ = false;
        bool restore_fullscreen_on_focus_ = false;
        WINDOWPLACEMENT temporary_placement_{};
        WINDOWPLACEMENT previous_{};
        bool restore_placement_ = false;
        DisplaySettings confirmed_;
        DisplaySettings draft_;
        DisplaySettings current_;
        std::vector<DisplayMonitor> monitors_;
        std::chrono::steady_clock::time_point deadline_;
    };
} // namespace gengine
