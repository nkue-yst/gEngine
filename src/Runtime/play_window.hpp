#pragma once
#include "assets.hpp"
#include "game.hpp"
#include "scene2d.hpp"
#include "view3d.hpp"
#include <filesystem>
#include <windows.h>

namespace gengine
{
    struct FrameMetrics
    {
        std::uint64_t frame = 0;
        std::uint64_t conversion_ns = 0;
        std::uint64_t render_ns = 0;
        std::uint64_t gpu_ns = 0;
        std::uint64_t gpu_frame = 0;
        std::uint64_t render_frame = 0;
        bool gpu_valid = false;
        std::uint64_t render_gpu_bytes = 0;
        std::uint64_t render_deferred_bytes = 0;
        std::uint64_t render_live_resources = 0;
        std::uint64_t render_capacity_growths = 0;
        std::uint64_t audio_voices = 0;
        std::uint64_t frame_ns = 0;
        std::uint64_t host_reserved_bytes = 0;
        std::uint64_t audio_pcm_bytes = 0;
        std::uint64_t render_reserved_bytes = 0;
        std::uint64_t audio_requests = 0;
        std::uint64_t audio_notifications = 0;
        AssetMetrics assets;
    };
    // 単独起動とEditorの子ビューで同じウィンドウ手続き・描画・入力を使う。
    class PlayWindow final
    {
      public:
        PlayWindow(HWND parent, std::span<const std::uint8_t> bytes, const std::filesystem::path& backends,
                   GameMode mode = GameMode::k2d, std::optional<SceneDocument> scene = {}, bool preview = false,
                   const AssetSource* assets = nullptr, const AssetSource* references = nullptr,
                   ProfileStorageOptions profile_storage = {});
        ~PlayWindow();
        HWND window() const;
        Game& game();
        void Reset(std::uint32_t preset);
        void Pause(bool paused);
        // VMを持たないプレビューだけを更新する。次元の変更には再作成が必要。
        void UpdatePreviewScene(const SceneDocument& scene, std::optional<View2d> view = {}, std::string selected = {});
        void UpdatePreviewScene3d(const SceneDocument& scene, View3d view, std::string selected);
        // 参照を変更したときだけGPU資源を交換する。forceは原本を再検証したときに使う。
        void UpdatePreviewAssets(const SceneDocument& scene, const AssetSource& source, bool force = false);
        // 実効解像度の確認にも使える。既定値は従来のMCP画像と互換にする。
        std::vector<std::uint8_t> Capture(std::uint32_t width = 640, std::uint32_t height = 360);
        // 連動中の実ウィンドウを主、左、右の順に返す。画像取得も同じ描画経路を使う。
        std::vector<HWND> ViewWindows() const;
        std::vector<std::uint8_t> CaptureView(std::uint32_t index, std::uint32_t width = 640,
                                              std::uint32_t height = 360);
        const FrameMetrics& metrics() const;
        void StartPerformance(PerformanceConfig config);
        void StopPerformance();
        bool PerformanceActive() const;
        PerformanceReport Performance();
        void RecordHostTime(Timing timing, std::uint64_t ns);

      private:
        PlayWindow(HWND parent, std::span<const std::uint8_t> bytes, const std::filesystem::path& backends,
                   GameMode mode, std::optional<SceneDocument> scene, bool preview, const AssetSource* assets,
                   const AssetSource* references, ProfileStorageOptions profile_storage, PlayWindow* primary,
                   std::uint32_t view_index);
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
    std::vector<std::uint8_t> ReadModule(const std::filesystem::path& path);
} // namespace gengine
