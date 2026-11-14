#include "play_window.hpp"
#include "frame_clock.hpp"
#include "game_presentation.hpp"
#include "game_ui.hpp"
#include "png.hpp"
#include "presentation_timing.hpp"
#include "preview3d.hpp"
#include "product_entry.hpp"
#include "scene3d.hpp"
#include <aengine/audio.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <rengine/renderer.hpp>

namespace gengine
{
    namespace
    {
        template <class T> T Take(std::expected<T, rengine::Error> result)
        {
            if (result.has_value() == false) {
                throw std::runtime_error(result.error().message);
            }
            return std::move(*result);
        }
        void Check(rengine::Result result)
        {
            if (result.has_value() == false) {
                throw std::runtime_error(result.error().message);
            }
        }
    } // namespace

    std::vector<std::uint8_t> ReadModule(const std::filesystem::path& path)
    {
        const auto absolute = std::filesystem::absolute(path);
        auto metrics = AssetMetrics{};
        const auto name = absolute.filename().u8string();
        return AssetSource(absolute.parent_path())
            .Read(std::string(reinterpret_cast<const char*>(name.data()), name.size()), 64 * 1024 * 1024, metrics);
    }

    struct PlayWindow::Impl
    {
        std::unique_ptr<Game> owned_game;
        Game* game = nullptr;
        PlayWindow* owner = nullptr;
        Impl* primary = nullptr;
        std::uint32_t view_index = 0;
        std::vector<std::unique_ptr<PlayWindow>> satellites;
        std::unique_ptr<AssetSource> retained_assets;
        std::unique_ptr<AssetSource> retained_references;
        std::filesystem::path backend_directory;
        bool arranging = false;
        bool temporary_windows = false;
        SceneDocument scene;
        std::optional<View2d> edit_view;
        std::optional<View3d> edit_view_3d;
        rengine::Material overlay_material{};
        std::string selected;
        HWND window = nullptr;
        HWND label = nullptr;
        bool embedded = false;
        bool destroying = false;
        bool ticking = false;
        FrameClock clock;
        std::uint64_t scheduler_lateness = 0;
        rengine::PresentationTiming presentation{};
        std::uint64_t presentation_queries = 0;
        std::uint64_t presentation_unavailable = 0;
        std::uint64_t presentation_disjoint = 0;
        std::uint64_t presentation_duplicates = 0;
        std::uint64_t presentation_gaps = 0;
        InputState input;
        InputRouter input_router;
        GamepadDevice gamepad;
        std::unique_ptr<rengine::Renderer> renderer;
        std::unique_ptr<GameUi> ui;
        std::unique_ptr<GamePresentation> presentation_art;
        std::unique_ptr<WindowDisplay> display;
        std::unique_ptr<ProfileStore> profile_store;
        std::optional<SavedProfile> submitted_profile;
        std::unique_ptr<GameAudio> audio;
        AudioSettings saved_audio;
        rengine::Material material{};
        rengine::Texture texture{};
        rengine::Mesh sphere{};
        // 専用素材だけを所有する。候補の作成失敗でも部分的なGPU資源を回収する。
        struct VisualResource
        {
            rengine::Renderer* renderer;
            rengine::Texture texture{};
            rengine::Material material{};
            rengine::Mesh mesh{};
            ~VisualResource()
            {
                // 後続のリソースも解放するため、破棄の戻り値は使わない。
                if (material.id != 0) {
                    static_cast<void>(renderer->DestroyMaterial(material));
                }
                if (texture.id != 0) {
                    static_cast<void>(renderer->DestroyTexture(texture));
                }
                if (mesh.id != 0) {
                    static_cast<void>(renderer->DestroyMesh(mesh));
                }
            }
        };
        std::array<std::unique_ptr<VisualResource>, 2> visuals;
        std::array<std::optional<std::string>, 2> visual_paths;

        rengine::Material grid_material{};
        std::array<rengine::Vertex, 60> grid{};
        rengine::Texture hud_texture{};
        rengine::Material hud_material{};
        std::wstring hud_text;
        std::array<float, 95> glyph_widths{};
        std::array<rengine::Instance, 128> hud_instances{};
        std::uint32_t hud_instance_count = 0;
        std::uint64_t hud_uploads = 0;
        FrameMetrics metrics;
        PerformanceCapture performance;
        std::vector<PerformanceCounter> captured_counters;
        std::array<std::uint64_t, 4> phase_frames{};
        std::uint64_t captured_ticks = 0;
        std::uint64_t paused_frames = 0;
        std::uint32_t render_width = 960;
        std::uint32_t render_height = 580;
        std::vector<rengine::Instance> instances;
        std::vector<rengine::Instance> actor_instances;
        std::vector<rengine::Instance> atlas_instances;
        std::vector<rengine::Instance> detail_instances;
        std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point last_frame = last;

        Impl(std::span<const std::uint8_t> bytes, GameMode mode, std::optional<SceneDocument> input_scene, bool preview,
             Game* shared = nullptr)
            : scene(input_scene.value_or(DefaultScene(mode == GameMode::k3d)))
        {
            ValidateScene(scene);
            game = shared;
            if (preview == false && shared == nullptr) {
                owned_game = std::make_unique<Game>(bytes, 1000000, mode, scene);
                game = owned_game.get();
            }
            instances.reserve(game && game->HasActors() ? 8194 + game->actors().capacity() : 8194);
            if (game && game->HasActors()) {
                actor_instances.reserve(game->actors().capacity());
            }
        }
        ~Impl()
        {
            // HWNDを破棄する前にGPUと音声の処理を終える。
            // 全画面解除がメッセージを送っても、破棄中の音声・描画へ再入させない。
            destroying = true;
            clock.Stop();
            satellites.clear();
            if (display && temporary_windows) {
                try {
                    display->EndTemporary();
                } catch (...) {
                    // 終了時は描画資源の破棄を続ける。OSが既に窓を回収した場合も再入させない。
                }
            }
            audio.reset();
            visuals = {};
            presentation_art.reset();
            ui.reset();
            display.reset();
            renderer.reset();
            if (window != nullptr) {
                DestroyWindow(window);
            }
        }
        void LoadVisuals(const SceneDocument& document, const AssetSource& source, bool preview, bool force = false)
        {
            const auto kinds = std::array{"player_spawn", "enemy_spawn"};
            for (auto slot = std::size_t{0}; slot < kinds.size(); ++slot) {
                const auto object = std::find_if(document.objects.begin(), document.objects.end(), [&](const auto& o) {
                    return o.kind == kinds[slot];
                });
                const auto path = object == document.objects.end() ? std::nullopt : object->visual;
                if (force == false && path == visual_paths[slot]) {
                    continue;
                }
                auto next = std::unique_ptr<VisualResource>{};
                if (path) {
                    try {
                        const auto is_3d = document.game == "3d";
                        if (AssetKind(*path) != (is_3d ? "mesh" : "image")) {
                            throw std::invalid_argument(is_3d ? "expected mesh asset" : "expected image asset");
                        }
                        const auto asset_bytes =
                            source.Read(*path, is_3d ? 4 * 1024 * 1024 : 16 * 1024 * 1024, metrics.assets);
                        next = std::make_unique<VisualResource>(renderer.get());
                        auto decoded_bytes = std::size_t{0};
                        if (is_3d) {
                            const auto mesh = DecodeMesh(asset_bytes);
                            decoded_bytes = mesh.vertices.capacity() * sizeof(rengine::Vertex) +
                                            mesh.indices.capacity() * sizeof(std::uint32_t);
                            next->mesh = Take(renderer->CreateMesh(
                                {mesh.vertices.data(), static_cast<std::uint32_t>(mesh.vertices.size()),
                                 mesh.indices.data(), static_cast<std::uint32_t>(mesh.indices.size()), nullptr, 0}));
                        }
                        else {
                            const auto image = DecodePng(asset_bytes);
                            decoded_bytes = image.pixels.capacity();
                            next->texture = Take(renderer->CreateTexture(
                                {image.width, image.height, 0, image.pixels.data(), image.pixels.size()}));
                            auto description = rengine::DefaultMaterial();
                            description.texture = next->texture;
                            // PNGの透明部分で、背後の弾や他の個体を黒く塗りつぶさない。
                            description.blend = 1;
                            next->material = Take(renderer->CreateMaterial(description));
                        }
                        const auto peak = asset_bytes.capacity() + decoded_bytes;
                        metrics.assets.decode_peak_bytes = std::max(metrics.assets.decode_peak_bytes, peak);
                        metrics.assets.transfer_peak_bytes = std::max(metrics.assets.transfer_peak_bytes, peak);
                        metrics.assets.peak_payload_bytes = std::max(metrics.assets.peak_payload_bytes, peak);
                    } catch (const std::exception& error) {
                        next.reset();
                        if (preview == false) {
                            throw std::runtime_error(object->id + ".visual: " + *path + ": " + error.what());
                        }
                        // 編集中だけは標準表示で修正を続けられる。理由は素材一覧の共通診断へ表示する。
                    }
                }
                visuals[slot] = std::move(next);
                visual_paths[slot] = path;
            }
        }

        bool GroupFocus(HWND focus) const
        {
            if (focus == window || IsChild(window, focus)) {
                return true;
            }
            return std::any_of(satellites.begin(), satellites.end(), [&](const auto& view) {
                return focus == view->window() || IsChild(view->window(), focus);
            });
        }

        void StopWindows(bool release)
        {
            if (primary != nullptr || arranging) {
                return;
            }
            arranging = true;
            try {
                for (const auto& view : satellites) {
                    ShowWindow(view->window(), SW_HIDE);
                }
                if (release) {
                    satellites.clear();
                }
                if (temporary_windows && display) {
                    display->EndTemporary();
                }
                temporary_windows = false;
                arranging = false;
            } catch (...) {
                arranging = false;
                throw;
            }
        }

        // 実窓の配置だけを変え、進行・音声・入力・保存の所有者は主窓から移さない。
        void SyncWindows()
        {
            if (primary != nullptr || embedded || game == nullptr || arranging) {
                return;
            }
            const auto& state = game->state();
            if (state.phase != Phase::kPlaying || state.windows.count == 0) {
                StopWindows(true);
                return;
            }
            // 選択カードは小さな戦闘窓へ押し込まず、元の表示サイズで読めるようにする。
            if (state.paused || game->interaction().request != 0 || IsIconic(window)) {
                StopWindows(false);
                return;
            }
            arranging = true;
            try {
                if (temporary_windows == false) {
                    if (display == nullptr || display->enabled() == false) {
                        throw std::runtime_error("window choreography requires standalone display support");
                    }
                    display->BeginTemporary();
                    temporary_windows = true;
                }
                while (satellites.size() + 1 < state.windows.count) {
                    const auto index = static_cast<std::uint32_t>(satellites.size() + 1);
                    satellites.emplace_back(new PlayWindow(nullptr, {}, backend_directory, GameMode::k2d, scene, false,
                                                           retained_assets.get(), retained_references.get(), {}, owner,
                                                           index));
                }
                while (satellites.size() + 1 > state.windows.count) {
                    satellites.pop_back();
                }
                auto info = MONITORINFO{};
                info.cbSize = sizeof(info);
                if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info) == FALSE) {
                    throw std::runtime_error("cannot locate game window work area");
                }
                const auto count = int(state.windows.count);
                const auto work_width = info.rcWork.right - info.rcWork.left;
                const auto work_height = info.rcWork.bottom - info.rcWork.top;
                const auto style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
                auto border = RECT{};
                AdjustWindowRectExForDpi(&border, style, FALSE, 0, GetDpiForWindow(window));
                const auto border_width = border.right - border.left;
                const auto border_height = border.bottom - border.top;
                const auto gap = 12;
                const auto base_width =
                    std::min({1920, int(work_width - 32 - gap * (count - 1) - border_width * count) / count,
                              int(float(work_height - 32 - border_height) * 16 / 9)});
                const auto client_width = std::max(160, int(float(base_width) * state.windows.scale));
                const auto client_height = client_width * 9 / 16;
                auto frame = RECT{0, 0, client_width, client_height};
                AdjustWindowRectExForDpi(&frame, style, FALSE, 0, GetDpiForWindow(window));
                const auto width = frame.right - frame.left;
                const auto height = frame.bottom - frame.top;
                const auto left = info.rcWork.left + (work_width - (width * count + gap * (count - 1))) / 2;
                const auto top = info.rcWork.top + (work_height - height) / 2;
                for (auto index = 0u; index < state.windows.count; ++index) {
                    const auto target = index == 0 ? window : satellites[index - 1]->window();
                    // 主窓は中央、追加窓は左右。ウィンドウの物理順と世界の順を一致させる。
                    const auto column = count == 3 ? (index == 0 ? 1 : index == 1 ? 0 : 2) : int(index);
                    const auto x = left + column * (width + gap);
                    auto current = RECT{};
                    GetWindowRect(target, &current);
                    if (current.left != x || current.top != top || current.right - current.left != width ||
                        current.bottom - current.top != height) {
                        if (SetWindowPos(target, nullptr, x, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE) ==
                            FALSE) {
                            throw std::runtime_error("cannot arrange linked game windows");
                        }
                    }
                    if (IsWindowVisible(target) == FALSE) {
                        ShowWindow(target, SW_SHOWNOACTIVATE);
                    }
                }
                arranging = false;
            } catch (...) {
                arranging = false;
                StopWindows(true);
                throw;
            }
        }

        void Pause(bool paused)
        {
            input = {};
            input_router.Clear();
            game->Pause(paused);
            if (paused && arranging == false) {
                StopWindows(false);
            }
            if (ui && ui->HasInputSettings() && game->state().phase == Phase::kPlaying) {
                if (paused) {
                    ui->ShowPause();
                }
                else {
                    ui->ResetMenu();
                }
            }
            last = std::chrono::steady_clock::now();
            clock.Start(window);
            audio->Pause(paused);
        }
        void Reset(std::uint32_t preset, bool begin = false)
        {
            StopWindows(true);
            audio->Reset();
            input = {};
            input_router.Clear();
            SyncProfile();
            game->Reset(preset);
            if (ui != nullptr) {
                ui->ResetMenu();
            }
            if (begin) {
                game->Begin();
            }
            last = std::chrono::steady_clock::now();
            clock.Start(window);
        }
        // 変更時だけ保存する。失敗の毎フレーム再試行はせず、F2または次の変更で再試行する。
        void SyncProfile(bool force = false)
        {
            if (profile_store == nullptr || game->state().phase == Phase::kFaulted) {
                return;
            }
            if (force == false && ui != nullptr && ui->PointerActive()) {
                return;
            }
            const auto value =
                SavedProfile{game->profile(), ui->language(), display ? display->confirmed() : DisplaySettings{},
                             audio ? audio->settings() : saved_audio, input_router.settings()};
            if (force || submitted_profile != value) {
                profile_store->Save(value);
                submitted_profile = value;
            }
        }
        // メニュー決定の入力を戦闘へ持ち越さず、共通の開始・終了経路へ渡す。
        bool ApplyUi(UiCommand command)
        {
            if (command == UiCommand::kUnhandled) {
                return false;
            }
            input = {};
            if (command == UiCommand::kBegin) {
                Reset(0, true);
            }
            if (command == UiCommand::kPause || command == UiCommand::kResume) {
                Pause(command == UiCommand::kPause);
            }
            if (command == UiCommand::kAbandon) {
                Reset(0);
            }
            if (command == UiCommand::kChosen) {
                input_router.Clear();
            }
            if (command == UiCommand::kClose) {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
            SyncProfile();
            last = std::chrono::steady_clock::now();
            return true;
        }
        bool MenuKey(std::uint32_t key)
        {
            const auto handled = ApplyUi(ui->Key(*game, key));
            // 方向の長押しは継続し、決定・取消しだけを次の画面へ持ち越さない。
            const auto direction = key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT || key == 'W' ||
                                   key == 'A' || key == 'S' || key == 'D';
            if (handled && direction == false) {
                input_router.Clear();
            }
            return handled;
        }
        void PollInput(double seconds)
        {
            if (ui == nullptr || ui->HasInputSettings() == false) {
                return;
            }
            // パッドはこのウィンドウが前面の間だけ読む。キーボードは窓のメッセージから更新する。
            const auto focus = GetFocus();
            const auto sample = GroupFocus(focus) ? gamepad.Poll() : PadSample{};
            const auto frame = input_router.Poll(sample, seconds);
            input = {};
            input.horizontal = frame.x;
            input.vertical = frame.y;
            input.fire = frame.focus;
            input.descend = frame.descend;
            input.ascend = frame.ascend;
            if (ui->CapturingInput()) {
                if (input_router.pressed_button() != 0 && ui->PadBinding(input_router.pressed_button())) {
                    input = {};
                    SyncProfile();
                    return;
                }
            }
            else if (frame.disconnected) {
                if (game->state().phase == Phase::kPlaying) {
                    Pause(true);
                }
                return;
            }
            const auto modal =
                game->state().phase != Phase::kPlaying || game->state().paused || game->interaction().request != 0;
            // A/BへPauseを割り当てても、メニュー中の決定・取消しを奪わない。
            const auto pad_menu_button =
                std::any_of(frame.menu_keys.begin(), frame.menu_keys.begin() + frame.menu_count, [](auto key) {
                    return key == VK_RETURN || key == VK_ESCAPE;
                });
            if (ui->CapturingInput() == false && frame.pause_pressed && game->state().phase == Phase::kPlaying &&
                (modal == false || pad_menu_button == false)) {
                ApplyUi(ui->TogglePause(*game));
                return;
            }
            if (modal) {
                for (auto index = 0u; index < frame.menu_count; ++index) {
                    MenuKey(frame.menu_keys[index]);
                    // 決定後の同じフレームに別のボタンを次の画面へ送らない。
                    if (frame.menu_keys[index] == VK_RETURN || frame.menu_keys[index] == VK_ESCAPE) {
                        break;
                    }
                }
            }
        }
        void UpdateHud(const std::wstring& text)
        {
            constexpr auto kGlyphWidth = 16;
            constexpr auto kGlyphHeight = 24;
            constexpr auto kAtlasWidth = kGlyphWidth * 95;
            if (text.size() + 1 > hud_instances.size() || std::any_of(text.begin(), text.end(), [](wchar_t value) {
                    return value < 32 || value > 126;
                })) {
                throw std::invalid_argument("HUD requires at most 127 printable ASCII characters");
            }
            // 現在のゲーム用HUDはASCIIだけを使う。全字形を同じフォントで一枚に準備する。
            if (hud_texture.id == 0) {
                auto info = BITMAPINFO{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = kAtlasWidth;
                info.bmiHeader.biHeight = -kGlyphHeight;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;
                auto* bits = static_cast<void*>(nullptr);
                const auto dc = CreateCompatibleDC(nullptr);
                const auto bitmap =
                    dc != nullptr ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
                if (bitmap == nullptr) {
                    if (dc != nullptr) {
                        DeleteDC(dc);
                    }
                    throw std::runtime_error("cannot rasterize HUD atlas");
                }
                const auto old = SelectObject(dc, bitmap);
                auto area = RECT{0, 0, kAtlasWidth, kGlyphHeight};
                FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                SetTextColor(dc, RGB(255, 255, 255));
                SetBkColor(dc, RGB(0, 0, 0));
                for (auto i = 0; i < 95; ++i) {
                    const auto glyph = static_cast<wchar_t>(i + 32);
                    auto size = SIZE{};
                    GetTextExtentPoint32W(dc, &glyph, 1, &size);
                    glyph_widths[i] = float(std::clamp<LONG>(size.cx, 1, kGlyphWidth));
                    auto cell = RECT{i * kGlyphWidth, 0, (i + 1) * kGlyphWidth, kGlyphHeight};
                    DrawTextW(dc, &glyph, 1, &cell, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                GdiFlush();
                // GDIを先に解放できるよう、所有する画素列へコピーする。
                auto pixels = std::vector<std::uint8_t>{};
                try {
                    pixels.resize(kAtlasWidth * kGlyphHeight * 4);
                    std::memcpy(pixels.data(), bits, pixels.size());
                } catch (...) {
                    SelectObject(dc, old);
                    DeleteObject(bitmap);
                    DeleteDC(dc);
                    throw;
                }
                SelectObject(dc, old);
                DeleteObject(bitmap);
                DeleteDC(dc);
                for (auto i = std::size_t{3}; i < pixels.size(); i += 4) {
                    pixels[i] = 255;
                }
                hud_texture =
                    Take(renderer->CreateTexture({kAtlasWidth, kGlyphHeight, 1, pixels.data(), pixels.size()}));
                auto hud = rengine::DefaultMaterial();
                hud.texture = hud_texture;
                hud_material = Take(renderer->CreateMaterial(hud));
                ++hud_uploads;
            }

            // 背景を空白の字形で覆い、各文字は同じテクスチャの対応する領域を参照する。
            auto width = 0.0f;
            for (const auto glyph : text) {
                width += glyph_widths[glyph - 32];
            }
            hud_instances[0] = rengine::Sprite(0, 0, 16, 0.6f);
            hud_instances[0].uv_rect[2] = 1.0f / float(kAtlasWidth);
            hud_instance_count = 1;
            auto x = (640.0f - width) * 0.5f;
            for (const auto glyph : text) {
                const auto index = static_cast<std::size_t>(glyph - 32);
                auto instance = rengine::Sprite(x / 40, 0, glyph_widths[index] / 40, 0.6f);
                instance.uv_rect[0] = float(index * kGlyphWidth) / float(kAtlasWidth);
                instance.uv_rect[2] = glyph_widths[index] / float(kAtlasWidth);
                hud_instances[hud_instance_count++] = instance;
                x += glyph_widths[index];
            }
            hud_text = text;
        }

        void Draw(rengine::Target target = {}, std::uint32_t target_width = 640, std::uint32_t target_height = 360)
        {
            const auto begin = std::chrono::steady_clock::now();
            if (target.id == 0) {
                metrics.conversion_ns = 0;
                metrics.render_ns = 0;
                metrics.gpu_valid = false;
                // 最小化中も設定確認と入力の時計は動かし、表示できないGPU描画だけを止める。
                if (IsIconic(GetAncestor(window, GA_ROOT))) {
                    return;
                }
            }
            auto rect = RECT{};
            GetClientRect(window, &rect);
            const auto width = target.id != 0 ? target_width : static_cast<std::uint32_t>(rect.right);
            const auto height = target.id != 0 ? target_height : static_cast<std::uint32_t>(rect.bottom);
            if (width == 0 || height == 0) {
                return;
            }
            if (target.id == 0 && (width != render_width || height != render_height)) {
                Check(renderer->Resize(width, height));
                render_width = width;
                render_height = height;
            }
            instances.clear();
            actor_instances.clear();
            atlas_instances.clear();
            detail_instances.clear();
            const auto add = [&](float x, float y, float rx, float ry, float red, float green, float blue,
                                 float angle = 0.0f) {
                auto instance = rengine::Sprite(0, 0, rx * 2, ry * 2, angle);
                // 回転後も論理座標を図形の中心に保ち、Y軸の向きをここだけで変換する。
                instance.world[12] = x + 8 - (instance.world[0] + instance.world[4]) * 0.5f;
                instance.world[13] = 4.5f - y - (instance.world[1] + instance.world[5]) * 0.5f;
                instance.color[0] = red;
                instance.color[1] = green;
                instance.color[2] = blue;
                instances.push_back(instance);
            };
            // 編集表示は文書だけを読む。VMも音声も作らず、試遊と同じ描画を使う。
            auto initial = GameState{};
            const auto find = [&](std::string_view kind) -> const SceneObject* {
                const auto it = std::find_if(scene.objects.begin(), scene.objects.end(), [&](const auto& o) {
                    return o.kind == kind;
                });
                return it == scene.objects.end() ? nullptr : &*it;
            };
            const auto* player = find("player_spawn");
            const auto* enemy = game != nullptr && game->HasActors() ? nullptr : find("enemy_spawn");
            const auto fallback = DefaultSceneObject(scene.game, "game_camera", "camera");
            const auto* camera = find("game_camera");
            if (camera == nullptr) {
                camera = &fallback;
            }
            if (player) {
                initial.player = {player->position[0], player->position[1],
                                  scene.game == "3d" ? player->position[2] : 0};
            }
            const auto& state = game ? game->state() : initial;
            const auto is_3d = scene.game == "3d";
            const auto enemy_position =
                enemy ? gvm::Vec3{enemy->position[0], enemy->position[1], is_3d ? enemy->position[2] : 0} : gvm::Vec3{};
            const auto dots = game ? std::span<const Dot>(game->Dots()) : std::span<const Dot>{};
            const auto editing_2d = game == nullptr && is_3d == false && edit_view.has_value();
            const auto editing_3d = game == nullptr && is_3d && edit_view_3d.has_value();
            const auto editing = editing_2d || editing_3d;
            const auto assign_sprite = [&](std::uint32_t index) {
                if (presentation_art == nullptr || index >= presentation_art->definition().sprites.size()) {
                    throw std::runtime_error("actor sprite is missing from visual manifest");
                }
                auto instance = instances.back();
                const auto& uv = presentation_art->definition().sprites[index];
                std::copy(uv.begin(), uv.end(), instance.uv_rect);
                atlas_instances.push_back(instance);
                instances.pop_back();
            };
            const auto linked = game && game->state().phase == Phase::kPlaying && state.windows.count != 0;
            const auto overview = linked && (embedded || state.paused || game->interaction().request != 0);
            const auto view_center = linked && overview == false ? state.windows.centers[view_index] : gvm::Vec2{};
            const auto view_width = linked       ? state.windows.size.x * (overview ? float(state.windows.count) : 1.f)
                                    : editing_2d ? float(edit_view->height * 16 / 9)
                                    : is_3d      ? 16.0f
                                                 : camera->view_size[0];
            const auto view_height = linked       ? state.windows.size.y * (overview ? float(state.windows.count) : 1.f)
                                     : editing_2d ? float(edit_view->height)
                                     : is_3d      ? 9.0f
                                                  : camera->view_size[1];
            if (is_3d) {
                // 論理座標をそのままワールドへ置き、半径だけを各インスタンスで変える。
                const auto add_sphere = [&](gvm::Vec3 position, float radius, float red, float green, float blue) {
                    auto instance = rengine::DefaultInstance();
                    instance.world[0] = radius;
                    instance.world[5] = radius;
                    instance.world[10] = radius;
                    instance.world[12] = position.x;
                    instance.world[13] = position.y;
                    instance.world[14] = position.z;
                    instance.color[0] = red;
                    instance.color[1] = green;
                    instance.color[2] = blue;
                    instances.push_back(instance);
                };
                if (player) {
                    add_sphere(state.player, 0.25f, 0.2f, 0.8f, 1);
                }
                if (enemy) {
                    add_sphere(enemy_position, 0.75f, 1, 0.2f, 0.3f);
                }
                for (const auto& dot : dots) {
                    add_sphere({dot.x, dot.y, dot.z}, 0.1f, dot.hostile ? 1.0f : 0.2f, dot.hostile ? 0.5f : 1.0f, 0.2f);
                }
                if (game != nullptr) {
                    for (const auto& actor : game->actors()) {
                        add_sphere(actor.position, actor.radius, actor.color.x, actor.color.y, actor.color.z);
                        if (actor.visual == 1) {
                            actor_instances.push_back(instances.back());
                            instances.pop_back();
                        }
                    }
                }
            }
            else {
                if (player) {
                    add(state.player.x, state.player.y, 0.18f, 0.18f, 0.2f, 0.8f, 1, 0.785398f);
                }
                if (enemy) {
                    add(enemy_position.x, enemy_position.y, 0.75f, 0.55f, 1, 0.2f, 0.3f);
                }
                for (const auto& dot : dots) {
                    add(dot.x, dot.y, dot.hostile ? 0.08f : 0.05f, dot.hostile ? 0.08f : 0.14f,
                        dot.hostile ? 1.0f : 0.2f, dot.hostile ? 0.5f : 1.0f, 0.2f, dot.hostile ? 0.785398f : 0.0f);
                    if (presentation_art) {
                        const auto& definition = presentation_art->definition();
                        const auto sprite = dot.hostile ? definition.hostile : definition.friendly;
                        if (sprite != 0) {
                            const auto& color = dot.hostile ? definition.hostile_color : definition.friendly_color;
                            std::copy(color.begin(), color.end(), instances.back().color);
                            assign_sprite(sprite - 1);
                        }
                    }
                }
                if (game != nullptr) {
                    for (const auto& actor : game->actors()) {
                        add(actor.position.x, actor.position.y, actor.radius, actor.radius, actor.color.x,
                            actor.color.y, actor.color.z, actor.angle);
                        instances.back().color[3] = actor.opacity;
                        if (actor.visual == 1) {
                            actor_instances.push_back(instances.back());
                            instances.pop_back();
                        }
                        else if (actor.visual >= 18) {
                            if (presentation_art == nullptr ||
                                actor.visual - 18 >= presentation_art->definition().detail_sprites.size()) {
                                throw std::runtime_error("detail sprite is missing from visual manifest");
                            }
                            auto instance = instances.back();
                            const auto& uv = presentation_art->definition().detail_sprites[actor.visual - 18];
                            std::copy(uv.begin(), uv.end(), instance.uv_rect);
                            detail_instances.push_back(instance);
                            instances.pop_back();
                        }
                        else if (actor.visual >= 2) {
                            assign_sprite(actor.visual - 2);
                        }
                    }
                }
            }
            if (editing_2d) {
                // 編集用の枠とハンドルもGPUへ描き、画像取得と実表示を一致させる。
                const auto mapping = MakeMapping2d(*edit_view, width, height);
                const auto unit = float(edit_view->height / mapping.height);
                const auto box = [&](float x, float y, float w, float h, float r, float g, float b) {
                    add(x, y - h, w, unit, r, g, b);
                    add(x, y + h, w, unit, r, g, b);
                    add(x - w, y, unit, h, r, g, b);
                    add(x + w, y, unit, h, r, g, b);
                };
                const auto& lo = scene.settings.bounds_min;
                const auto& hi = scene.settings.bounds_max;
                box((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (hi[0] - lo[0]) / 2, (hi[1] - lo[1]) / 2, .2f, .35f, .4f);
                for (const auto& object : scene.objects) {
                    if (object.kind == "game_camera") {
                        box(0, 0, object.view_size[0] / 2, object.view_size[1] / 2, object.id == selected ? 1.0f : .5f,
                            .7f, .3f);
                    }
                    else if (object.id == selected) {
                        const auto x = object.position[0];
                        const auto y = object.position[1];
                        const auto direction = HandleDirection2d(mapping, mapping.ToPixel({x, y}));
                        const auto horizontal = float(direction[0]) * unit;
                        const auto vertical = float(direction[1]) * unit;
                        box(x, y, object.kind == "enemy_spawn" ? .8f : .3f, object.kind == "enemy_spawn" ? .6f : .3f, 1,
                            .9f, .2f);
                        add(x + 44 * horizontal, y, 16 * unit, 2 * unit, 1, .3f, .2f);
                        add(x + 60 * horizontal, y, 4 * unit, 5 * unit, 1, .3f, .2f);
                        add(x, y + 44 * vertical, 2 * unit, 16 * unit, .2f, 1, .3f);
                        add(x, y + 60 * vertical, 5 * unit, 4 * unit, .2f, 1, .3f);
                        add(x + 16 * horizontal, y + 16 * vertical, 8 * unit, 8 * unit, 1, .85f, .2f);
                    }
                }
            }
            if (is_3d == false) {
                for (auto& instance : detail_instances) {
                    instance.world[12] += view_width * .5f - 8 - view_center.x;
                    instance.world[13] += view_height * .5f - 4.5f + view_center.y;
                }
                for (auto& instance : atlas_instances) {
                    instance.world[12] += view_width * .5f - 8 - view_center.x;
                    instance.world[13] += view_height * .5f - 4.5f + view_center.y;
                }
                for (auto& instance : actor_instances) {
                    instance.world[12] += view_width * .5f - 8 - view_center.x;
                    instance.world[13] += view_height * .5f - 4.5f + view_center.y;
                }
                for (auto& instance : instances) {
                    instance.world[12] += view_width * 0.5f - 8 - view_center.x;
                    instance.world[13] += view_height * 0.5f - 4.5f + view_center.y;
                    if (editing_2d) {
                        instance.world[12] -= float(edit_view->center[0]);
                        instance.world[13] += float(edit_view->center[1]);
                    }
                }
            }
            auto draw = rengine::Draw{};
            draw.kind = is_3d ? 0u : 1u;
            draw.mesh = sphere;
            draw.material = material;
            draw.instances = instances.data();
            draw.instance_count = static_cast<std::uint32_t>(instances.size());
            auto pass = rengine::DefaultPass();
            pass.target = target;
            rengine::Orthographic(pass.view_projection, view_width, view_height);
            if (is_3d) {
                if (editing_3d) {
                    EditorCamera3d(pass.view_projection, MakeMapping3d(*edit_view_3d, width, height));
                }
                else {
                    GameCamera(pass.view_projection, 16.0f / 9.0f, *camera);
                }
            }
            pass.draws = &draw;
            pass.draw_count = 1;
            const auto scale = std::min(float(width) / 16, float(height) / 9);
            pass.viewport[0] = (float(width) - scale * 16) / 2;
            pass.viewport[1] = (float(height) - scale * 9) / 2;
            pass.viewport[2] = scale * 16;
            pass.viewport[3] = scale * 9;

            auto text = std::wstring(is_3d ? L"WASD: XZ   Q/E: down/up   Space: fire   P: pause   HP "
                                           : L"WASD: move   Space: fire   P: pause   HP ") +
                        std::to_wstring(state.hp) + L" / Enemy " + std::to_wstring(state.enemy_hp);
            if (game != nullptr && state.phase == Phase::kStart) {
                text = is_3d ? L"gEngine 3D   Enter: START   WASD: XZ   Q/E: down/up   Space: fire"
                             : L"gEngine 2D   Enter: START   WASD: move   Space: fire";
            }
            if (state.phase == Phase::kResult) {
                text = state.outcome == 1 ? L"CLEAR   Enter: retry   Escape: title"
                                          : L"GAME OVER   Enter: retry   Escape: title";
            }
            if (game == nullptr) {
                text = editing ? L"2D edit | X: red  Y: green | wheel: zoom | middle: pan" : L"Scene preview";
                if (scene.objects.size() != 3) {
                    text += L" - player / enemy / camera required for play";
                }
            }
            if (state.paused) {
                text = L"PAUSED   P: resume   Escape: title";
            }
            if (state.phase == Phase::kFaulted) {
                text = L"Runtime error   Escape: reset";
            }
            // 文字画像は一度だけGPUへ送り、HPやフェーズの変化では文字の配置だけを更新する。
            if (editing == false && primary == nullptr && ui == nullptr && text != hud_text) {
                UpdateHud(text);
            }
            auto hud_draw = rengine::Draw{};
            hud_draw.kind = 1;
            hud_draw.material = hud_material;
            hud_draw.instances = hud_instances.data();
            hud_draw.instance_count = hud_instance_count;
            if (ui != nullptr) {
                hud_draw = ui->Draw(*game, profile_store ? profile_store->status() : SaveStatus::kMemory);
            }
            // 奥行きのある世界を先に描き、画面上の文字は色を消さず別の正射影で重ねる。
            auto grid_draw = rengine::Draw{};
            grid_draw.kind = 2;
            grid_draw.material = grid_material;
            grid_draw.lines = grid.data();
            grid_draw.line_vertex_count = static_cast<std::uint32_t>(grid.size());
            auto edit_geometry = std::optional<PreviewGeometry3d>{};
            auto edit_lines = grid_draw;
            auto overlay_draw = rengine::Draw{};
            if (editing_3d) {
                edit_geometry = BuildPreview3d(scene, MakeMapping3d(*edit_view_3d, width, height), selected);
                edit_lines.lines = edit_geometry->lines.data();
                edit_lines.line_vertex_count = edit_geometry->line_count;
                overlay_draw.kind = 1;
                overlay_draw.material = overlay_material;
                overlay_draw.instances = edit_geometry->overlays.data();
                overlay_draw.instance_count = edit_geometry->overlay_count;
            }
            // 専用素材の対象だけ描画を分け、弾と標準表示は連続した配列のまま一括描画する。
            auto world_draws = std::array<rengine::Draw, 8>{};
            auto count = std::uint32_t{0};
            auto first = std::uint32_t{0};
            auto actor = std::uint32_t{0};
            for (auto slot = std::size_t{0}; slot < visuals.size(); ++slot) {
                if ((slot == 0 ? player : enemy) == nullptr) {
                    continue;
                }
                if (visuals[slot]) {
                    if (actor > first) {
                        auto batch = draw;
                        batch.instances += first;
                        batch.instance_count = actor - first;
                        world_draws[count++] = batch;
                    }
                    auto assigned = draw;
                    assigned.instances += actor;
                    assigned.instance_count = 1;
                    assigned.mesh = is_3d ? visuals[slot]->mesh : sphere;
                    assigned.material = is_3d ? material : visuals[slot]->material;
                    world_draws[count++] = assigned;
                    first = actor + 1;
                }
                ++actor;
            }
            if (draw.instance_count > first) {
                draw.instances += first;
                draw.instance_count -= first;
                world_draws[count++] = draw;
            }
            if (is_3d) {
                world_draws[count++] = grid_draw;
                if (editing_3d) {
                    world_draws[count++] = edit_lines;
                }
            }
            if (actor_instances.empty() == false) {
                auto assigned = rengine::Draw{};
                assigned.kind = is_3d ? 0u : 1u;
                assigned.mesh = is_3d && visuals[1] ? visuals[1]->mesh : sphere;
                assigned.material = is_3d == false && visuals[1] ? visuals[1]->material : material;
                assigned.instances = actor_instances.data();
                assigned.instance_count = static_cast<std::uint32_t>(actor_instances.size());
                world_draws[count++] = assigned;
            }
            pass.draws = world_draws.data();
            if (atlas_instances.empty() == false) {
                auto assigned = rengine::Draw{};
                assigned.kind = 1;
                assigned.material = presentation_art->atlas_material();
                assigned.instances = atlas_instances.data();
                assigned.instance_count = static_cast<std::uint32_t>(atlas_instances.size());
                world_draws[count++] = assigned;
            }
            pass.draw_count = count;
            if (detail_instances.empty() == false) {
                auto assigned = rengine::Draw{};
                assigned.kind = 1;
                assigned.material = presentation_art->detail_material();
                assigned.instances = detail_instances.data();
                assigned.instance_count = static_cast<std::uint32_t>(detail_instances.size());
                world_draws[count++] = assigned;
                pass.draw_count = count;
            }
            auto hud_pass = pass;
            rengine::Orthographic(hud_pass.view_projection, 16, 9);
            hud_pass.clear_color_enabled = 0;
            hud_pass.clear_depth_enabled = 0;
            hud_pass.draws = &hud_draw;
            hud_pass.draw_count = primary == nullptr ? 1u : 0u;
            if (editing_3d) {
                hud_pass.draws = &overlay_draw;
                hud_pass.draw_count = edit_geometry->overlay_count == 0 ? 0u : 1u;
            }
            // 装飾は画面座標、戦闘は既存のカメラ座標で描く。静止編集には読み込まない。
            auto backdrop = rengine::Draw{};
            auto logo = rengine::Draw{};
            auto backdrop_pass = hud_pass;
            auto hud_draws = std::array{hud_draw, logo};
            if (presentation_art) {
                backdrop = presentation_art->Backdrop(state.phase != Phase::kStart, state.presentation.scene);
                backdrop_pass.clear_color_enabled = 1;
                backdrop_pass.draws = &backdrop;
                backdrop_pass.draw_count = backdrop.instance_count;
                if (backdrop.instance_count != 0) {
                    pass.clear_color_enabled = 0;
                }
                // タイトルと結果には停止した弾を残さず、戦闘中の選択・停止だけ背景の状況を見せる。
                if (state.phase == Phase::kStart || state.phase == Phase::kResult) {
                    pass.draw_count = 0;
                }
                if (ui && ui->TitleMenuVisible(*game)) {
                    hud_draws[1] = presentation_art->Logo(1.05f, 1.2f, 5.8f);
                    hud_pass.draws = hud_draws.data();
                    hud_pass.draw_count = hud_draws[1].instance_count != 0 ? 2u : 1u;
                }
            }
            const auto passes = std::array{backdrop_pass, pass, hud_pass};
            const auto has_backdrop = backdrop.instance_count != 0;
            const auto first_pass = has_backdrop ? 0u : 1u;
            const auto submitted = std::chrono::steady_clock::now();
            Check(renderer->Render(
                {passes.data() + first_pass, editing_2d ? 1u : 3u - first_pass, target.id == 0 ? 1u : 0u}));
            if (target.id == 0) {
                const auto ended = std::chrono::steady_clock::now();
                metrics.conversion_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(submitted - begin).count());
                metrics.render_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(ended - submitted).count());
                metrics.host_reserved_bytes = instances.capacity() * sizeof(rengine::Instance) + 8192 * sizeof(Dot) +
                                              actor_instances.capacity() * sizeof(rengine::Instance) + sizeof(grid) +
                                              atlas_instances.capacity() * sizeof(rengine::Instance) +
                                              detail_instances.capacity() * sizeof(rengine::Instance) +
                                              sizeof(hud_instances);
                const auto stats = Take(renderer->Stats());
                metrics.render_reserved_bytes = stats.cpu_reserved_bytes;
                metrics.render_gpu_bytes = stats.gpu_resource_bytes;
                metrics.render_deferred_bytes = stats.deferred_bytes;
                metrics.render_live_resources = stats.live_resources;
                metrics.render_capacity_growths = stats.frame_capacity_growths;
                metrics.gpu_ns = stats.gpu_nanoseconds;
                metrics.gpu_frame = stats.gpu_frame_index;
                metrics.render_frame = stats.frame_index;
                metrics.gpu_valid = stats.gpu_time_valid != 0;
            }
        }
        // 表示統計の欠測は標本へ混ぜず、理由別の件数を残して性能結果を判断できるようにする。
        void CapturePresentation()
        {
            if (renderer->HasPresentationTiming() == false) {
                return;
            }
            const auto observed = Take(renderer->ReadPresentationTiming());
            const auto present_interval = PresentationInterval(presentation, observed);
            if (observed.sequence != presentation.sequence) {
                performance.Host(Timing::kPresentCpu, observed.cpu_nanoseconds);
            }
            if (present_interval) {
                performance.Host(Timing::kPresentationInterval, *present_interval);
            }
            if (performance.collecting()) {
                ++presentation_queries;
                presentation_unavailable += observed.available == 0 ? 1 : 0;
                presentation_disjoint += observed.disjoint != 0 ? 1 : 0;
                const auto both_available = observed.available != 0 && presentation.available != 0;
                const auto repeated = both_available && observed.present_count == presentation.present_count;
                presentation_duplicates += repeated ? 1 : 0;
                presentation_gaps +=
                    both_available && repeated == false && present_interval.has_value() == false ? 1 : 0;
            }
            presentation = observed;
        }

        void Tick()
        {
            // 確認期限はゲームの停止や最小化に依存させない。
            if (display != nullptr) {
                display->Poll();
            }
            if (game == nullptr) {
                Draw();
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            const auto frame_lateness = scheduler_lateness;
            // ゲーム時間のリセット・Pause・メニュー遷移から、描画開始間隔の計測を独立させる。
            const auto frame_interval = std::chrono::duration<double>(now - last_frame).count();
            last_frame = now;
            const auto interval = std::chrono::duration<double>(now - last).count();
            const auto previous_time = last;
            PollInput(interval);
            // 入力がランをリセットした後から更新費用を測り、旧ランの累積値を引かない。
            const auto tick_before = game->state().tick;
            const auto update_before = game->state().total_update_ns;
            const auto contact_before = game->state().total_contact_ns;
            const auto apply_before = game->state().total_apply_ns;
            const auto request = game->interaction().request;
            // メニュー操作が時刻を更新したフレームは、遷移前の経過時間を戦闘へ渡さない。
            game->Advance(last == previous_time ? interval : 0., input);
            const auto advanced_ticks = game->state().tick - tick_before;
            const auto update_ns = game->state().total_update_ns - update_before;
            const auto contact_ns = game->state().total_contact_ns - contact_before;
            const auto apply_ns = game->state().total_apply_ns - apply_before;
            if (ui && ui->HasInputSettings() && game->interaction().request != 0 &&
                request != game->interaction().request) {
                input = {};
                input_router.Clear();
            }
            last = now;
            SyncWindows();
            SyncProfile();
            const auto audio_begin = std::chrono::steady_clock::now();
            audio->Update(game->state().phase, game->music_track(), game->TakeSounds(), game->state().paused);
            metrics.audio_requests = audio->requests();
            metrics.audio_notifications = audio->notifications();
            const auto audio_end = std::chrono::steady_clock::now();
            Draw();
            // 追加窓は主窓の更新済みスナップショットだけを描き、時計と音声を持たない。
            for (const auto& view : satellites) {
                if (temporary_windows) {
                    view->impl_->Draw();
                    metrics.conversion_ns += view->metrics().conversion_ns;
                    metrics.render_ns += view->metrics().render_ns;
                }
                // Pause中に再利用のため保持する追加窓も、保持量から除外しない。
                const auto& extra = view->metrics();
                metrics.host_reserved_bytes += extra.host_reserved_bytes;
                metrics.render_reserved_bytes += extra.render_reserved_bytes;
                metrics.render_gpu_bytes += extra.render_gpu_bytes;
                metrics.render_deferred_bytes += extra.render_deferred_bytes;
                metrics.render_live_resources += extra.render_live_resources;
                metrics.render_capacity_growths += extra.render_capacity_growths;
            }
            metrics.frame_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - now).count());
            ++metrics.frame;
            const auto audio_stats = audio->Stats();
            metrics.audio_pcm_bytes = audio_stats.pcm_bytes;
            metrics.audio_voices = audio_stats.voices;

            // 固定更新が0回なら0、複数回なら合計を記録し、直前のtickを重複集計しない。
            if (performance.active()) {
                performance.Host(Timing::kSchedulerLateness, frame_lateness);
                CapturePresentation();
                const auto& after = game->state();
                auto times = FrameTimes{};
                times[static_cast<std::size_t>(Timing::kFrame)] = metrics.frame_ns;
                times[static_cast<std::size_t>(Timing::kInterval)] = static_cast<std::uint64_t>(frame_interval * 1e9);
                times[static_cast<std::size_t>(Timing::kVm)] = update_ns;
                times[static_cast<std::size_t>(Timing::kContact)] = contact_ns;
                times[static_cast<std::size_t>(Timing::kApply)] = apply_ns;
                times[static_cast<std::size_t>(Timing::kConversion)] = metrics.conversion_ns;
                times[static_cast<std::size_t>(Timing::kRender)] = metrics.render_ns;
                times[static_cast<std::size_t>(Timing::kGpu)] = metrics.gpu_ns;
                times[static_cast<std::size_t>(Timing::kAudio)] = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(audio_end - audio_begin).count());
                if (performance.collecting()) {
                    ++phase_frames[static_cast<std::size_t>(after.phase)];
                    captured_ticks += advanced_ticks;
                    paused_frames += after.paused ? 1 : 0;
                }
                performance.Frame(times, metrics.gpu_frame, metrics.gpu_valid, metrics.render_frame);
                if (performance.active() == false) {
                    captured_counters = Counters();
                }
            }
        }

        std::vector<PerformanceCounter> Counters()
        {
            auto result = game->MemoryCounters();
            for (const auto& item : std::vector<PerformanceCounter>{
                     {"host_reserved_bytes", metrics.host_reserved_bytes},
                     {"render_cpu_reserved_bytes", metrics.render_reserved_bytes},
                     {"render_gpu_resource_bytes", metrics.render_gpu_bytes},
                     {"render_deferred_bytes", metrics.render_deferred_bytes},
                     {"render_live_resources", metrics.render_live_resources},
                     {"render_capacity_growths", metrics.render_capacity_growths},
                     {"hud_texture_uploads", hud_uploads},
                     {"audio_pcm_bytes", metrics.audio_pcm_bytes},
                     {"audio_voices", metrics.audio_voices},
                     {"asset_read_peak_bytes", metrics.assets.read_peak_bytes},
                     {"asset_decode_peak_bytes", metrics.assets.decode_peak_bytes},
                     {"asset_transfer_peak_bytes", metrics.assets.transfer_peak_bytes},
                     {"start_frames", phase_frames[0]},
                     {"playing_frames", phase_frames[1]},
                     {"result_frames", phase_frames[2]},
                     {"faulted_frames", phase_frames[3]},
                     {"simulation_ticks", captured_ticks},
                     {"paused_frames", paused_frames},
                     {"frame_timer_high_resolution", clock.high_resolution() ? 1u : 0u},
                     {"frame_requests_coalesced", clock.coalesced()},
                     {"presentation_timing_supported", renderer->HasPresentationTiming() ? 1u : 0u},
                     {"presentation_queries", presentation_queries},
                     {"presentation_unavailable", presentation_unavailable},
                     {"presentation_disjoint", presentation_disjoint},
                     {"presentation_duplicates", presentation_duplicates},
                     {"presentation_gaps", presentation_gaps},
                     {"presentation_count", presentation.present_count},
                     {"presentation_refresh_count", presentation.present_refresh_count},
                     {"presentation_sync_refresh_count", presentation.sync_refresh_count},
                     {"presentation_sync_qpc", presentation.sync_qpc},
                     {"presentation_qpc_frequency", presentation.qpc_frequency},
                     {"width", render_width},
                     {"height", render_height},
                     {"preset", game->state().preset},
                     {"vsync", display == nullptr || display->current().vsync ? 1u : 0u}}) {
                result.push_back(item);
            }
            return result;
        }
        static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
        {
            auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (message == WM_NCCREATE) {
                self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                self->window = hwnd;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (self == nullptr || self->destroying) {
                return DefWindowProcW(hwnd, message, wp, lp);
            }
            try {
                if (self->primary != nullptr) {
                    if (message == FrameClock::kMessage) {
                        return 0;
                    }
                    if (message == WM_KEYDOWN || message == WM_KEYUP) {
                        return SendMessageW(self->primary->window, message, wp, lp);
                    }
                    if (message == WM_CLOSE || (message == WM_SYSCOMMAND && (wp & 0xfff0) == SC_CLOSE) ||
                        (message == WM_SIZE && wp == SIZE_MINIMIZED)) {
                        self->primary->Pause(true);
                        self->primary->StopWindows(false);
                        SetFocus(self->primary->window);
                        return 0;
                    }
                    if (message == WM_KILLFOCUS && self->primary->arranging == false &&
                        self->primary->GroupFocus(reinterpret_cast<HWND>(wp)) == false) {
                        self->primary->Pause(true);
                    }
                    if (message == WM_LBUTTONDOWN &&
                        (self->game->state().paused || self->game->interaction().request != 0)) {
                        SetFocus(self->primary->window);
                        return 0;
                    }
                }
                // PIEに届くキーと文字をゲーム側で受け取り、Editorのボタン操作に奪われないようにする。
                if (message == WM_GETDLGCODE) {
                    return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
                }

                // ゲーム操作は押下・解放で処理するため、文字入力としての既定処理は行わない。
                if (message == WM_CHAR || message == WM_DEADCHAR) {
                    return 0;
                }

                if (message == FrameClock::kMessage) {
                    self->scheduler_lateness = self->clock.Consume(static_cast<std::uint64_t>(lp));
                    if (self->ticking || (self->display != nullptr && self->display->changing())) {
                        return 0;
                    }
                    // DXGIや表示変更がメッセージを処理しても、進行中のフレームへ再入しない。
                    self->ticking = true;
                    self->Tick();
                    self->ticking = false;
                    return 0;
                }
                // 静止プレビューは変更・再表示時だけ描く。待機中にGPUへ同じ絵を送り続けない。
                if (message == WM_PAINT && self->game == nullptr) {
                    auto paint = PAINTSTRUCT{};
                    BeginPaint(hwnd, &paint);
                    EndPaint(hwnd, &paint);
                    if (self->renderer != nullptr) {
                        self->Draw();
                    }
                    return 0;
                }
                if (message == WM_LBUTTONUP || message == WM_CAPTURECHANGED) {
                    if (self->ui != nullptr && self->ui->PointerActive()) {
                        self->ui->ReleasePointer();
                        self->SyncProfile();
                    }
                    if (message == WM_LBUTTONUP && GetCapture() == hwnd) {
                        ReleaseCapture();
                    }
                    return 0;
                }
                if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE) {
                    if (message == WM_LBUTTONDOWN) {
                        SetFocus(hwnd);
                    }
                    if (self->ui != nullptr) {
                        auto rect = RECT{};
                        GetClientRect(hwnd, &rect);
                        if (rect.right > 0 && rect.bottom > 0) {
                            // 描画と同じ16:9の領域へ戻し、黒帯をメニューの操作範囲に含めない。
                            const auto scale = std::min(float(rect.right) / 16, float(rect.bottom) / 9);
                            const auto x =
                                (float(static_cast<short>(LOWORD(lp))) - (float(rect.right) - scale * 16) / 2) / scale;
                            const auto y =
                                (float(static_cast<short>(HIWORD(lp))) - (float(rect.bottom) - scale * 9) / 2) / scale;
                            if (message == WM_MOUSEMOVE) {
                                // ドラッグ中の音量は即時反映し、保存は解放時にまとめる。
                                self->ui->PointerMove(*self->game, x, y);
                            }
                            else {
                                if (self->ApplyUi(self->ui->Click(*self->game, x, y))) {
                                    self->input_router.Clear();
                                }
                                if (self->ui->PointerActive()) {
                                    SetCapture(hwnd);
                                }
                            }
                        }
                    }
                    return 0;
                }
                if (message == WM_KILLFOCUS && self->audio != nullptr && self->arranging == false &&
                    self->GroupFocus(reinterpret_cast<HWND>(wp)) == false) {
                    self->Pause(true);
                    self->input_router.Clear(false);
                    self->audio->StopPreview();
                }
                if (message == WM_SETFOCUS && self->audio != nullptr && self->game->state().phase != Phase::kPlaying) {
                    self->Pause(false);
                }
                if (message == WM_DISPLAYCHANGE && self->display != nullptr) {
                    self->display->RefreshMonitors();
                }
                if (message == WM_DPICHANGED && self->embedded == false) {
                    const auto* rect = reinterpret_cast<const RECT*>(lp);
                    if (self->display == nullptr || (self->display->changing() == false &&
                                                     self->display->confirmed().mode == DisplayMode::kWindowed)) {
                        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                                     rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER);
                    }
                    return 0;
                }
                if (message == WM_SIZE) {
                    if (self->game == nullptr) {
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    if (self->label != nullptr) {
                        MoveWindow(self->label, 0, 0, LOWORD(lp), 24, TRUE);
                    }
                    if (wp == SIZE_MINIMIZED && self->audio != nullptr) {
                        self->Pause(true);
                    }
                }
                if ((message == WM_KEYDOWN || message == WM_KEYUP) && self->game != nullptr) {
                    const auto down = message == WM_KEYDOWN;
                    if (self->ui && self->ui->HasInputSettings()) {
                        // フォーカスが戻った後のOSの反復を新規押下として扱わない。
                        if (down && (lp & (1LL << 30)) != 0) {
                            return 0;
                        }
                        const auto key = static_cast<std::uint32_t>(wp);
                        if (self->input_router.Key(key, down)) {
                            // キーボードのPauseはUIへ即時に渡し、次のPollで二重実行しない。
                            self->input_router.TakeKeyPause();
                            if (self->MenuKey(key) == false) {
                                if (key == VK_F1) {
                                    self->ui->SwitchLanguage();
                                    self->SyncProfile();
                                }
                                if (key == VK_F2) {
                                    self->SyncProfile(true);
                                }
                            }
                        }
                        return 0;
                    }
                    if (down && (lp & (1LL << 30)) == 0 && self->ui != nullptr) {
                        if (wp == VK_F2) {
                            self->SyncProfile(true);
                            return 0;
                        }
                        if (self->ApplyUi(self->ui->Key(*self->game, static_cast<std::uint32_t>(wp)))) {
                            return 0;
                        }
                    }
                    if (wp == 'W') {
                        self->input.up = down;
                    }
                    if (wp == 'A') {
                        self->input.left = down;
                    }
                    if (wp == 'S') {
                        self->input.down = down;
                    }
                    if (wp == 'D') {
                        self->input.right = down;
                    }
                    if (wp == VK_SPACE) {
                        self->input.fire = down;
                    }
                    if (wp == 'Q') {
                        self->input.descend = down;
                    }
                    if (wp == 'E') {
                        self->input.ascend = down;
                    }
                    if (down && (lp & (1LL << 30)) == 0) {
                        if (wp == VK_F1 && self->ui != nullptr) {
                            self->ui->SwitchLanguage();
                            self->SyncProfile();
                        }
                        if (wp >= '1' && wp <= '3' && self->game->interaction().request != 0) {
                            const auto request = self->game->interaction().request;
                            if (self->game->Choose(request, static_cast<std::uint32_t>(wp - '1'))) {
                                self->input = {};
                                self->last = std::chrono::steady_clock::now();
                            }
                        }
                        if (wp == VK_RETURN && self->game->state().phase != Phase::kPlaying) {
                            self->Reset(self->game->state().preset, true);
                        }
                        if (wp == VK_ESCAPE) {
                            self->Reset(0);
                        }
                        if (wp == 'P') {
                            self->Pause(self->game->state().paused == false);
                        }
                    }
                    return 0;
                }
                // 利用者による閉じる操作だけ確認する。ホストの停止要求はWM_CLOSEのまま受け付ける。
                if (message == WM_SYSCOMMAND && (wp & 0xfff0) == SC_CLOSE && self->ui &&
                    self->ui->RequestClose(*self->game)) {
                    if (self->game->state().phase == Phase::kPlaying) {
                        self->Pause(true);
                    }
                    self->input_router.Clear();
                    return 0;
                }
                if (message == WM_CLOSE) {
                    self->StopWindows(true);
                    if (self->ui != nullptr) {
                        self->ui->ReleasePointer();
                    }
                    if (self->display != nullptr) {
                        self->display->Cancel();
                    }
                    self->SyncProfile();
                    ShowWindow(hwnd, SW_HIDE);
                    if (self->embedded == false) {
                        PostQuitMessage(0);
                    }
                    return 0;
                }
            } catch (const std::exception& error) {
                self->ticking = false;
                self->clock.Stop();
                if (self->game != nullptr) {
                    self->game->Fail(error.what());
                }
                self->StopWindows(true);
                if (self->audio != nullptr) {
                    self->audio->Stop();
                }
                SetWindowTextA(hwnd, error.what());
            }
            return DefWindowProcW(hwnd, message, wp, lp);
        }
    };

    PlayWindow::PlayWindow(HWND parent, std::span<const std::uint8_t> bytes, const std::filesystem::path& backends,
                           GameMode mode, std::optional<SceneDocument> scene, bool preview, const AssetSource* assets,
                           const AssetSource* references, ProfileStorageOptions profile_storage)
        : PlayWindow(parent, bytes, backends, mode, std::move(scene), preview, assets, references,
                     std::move(profile_storage), nullptr, 0)
    {
    }

    PlayWindow::PlayWindow(HWND parent, std::span<const std::uint8_t> bytes, const std::filesystem::path& backends,
                           GameMode mode, std::optional<SceneDocument> scene, bool preview, const AssetSource* assets,
                           const AssetSource* references, ProfileStorageOptions profile_storage, PlayWindow* primary,
                           std::uint32_t view_index)
        : impl_(
              std::make_unique<Impl>(bytes, mode, std::move(scene), preview, primary ? primary->impl_->game : nullptr))
    {
        impl_->owner = this;
        impl_->primary = primary ? primary->impl_.get() : nullptr;
        impl_->view_index = view_index;
        // PIE・単独起動・静止プレビューが同じ素材デコーダーを使う。入力はこの構築中だけ借用する。
        auto defaults = std::optional<AssetSource>{};
        if (assets == nullptr) {
            defaults.emplace(backends.parent_path() / "content");
            assets = &*defaults;
        }
        if (references == nullptr) {
            references = assets;
        }
        if (primary == nullptr && impl_->game && impl_->game->HasWindowViews()) {
            impl_->retained_assets = assets->Reopen();
            impl_->retained_references = references->Reopen();
            impl_->backend_directory = backends;
        }
        auto wc = WNDCLASSW{};
        wc.lpfnWndProc = Impl::Procedure;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"gEnginePlayView";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(kProductIconId));
        RegisterClassW(&wc);
        impl_->embedded = parent != nullptr;
        const auto style = parent != nullptr
                               ? WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN
                               : WS_OVERLAPPEDWINDOW | (primary == nullptr ? WS_VISIBLE : 0) | WS_CLIPCHILDREN;
        const auto hwnd =
            CreateWindowW(wc.lpszClassName, L"gEngine GameRunner", style, parent != nullptr ? 0 : CW_USEDEFAULT,
                          parent != nullptr ? 0 : CW_USEDEFAULT, 960, 580, parent, nullptr, wc.hInstance, impl_.get());
        if (hwnd == nullptr) {
            throw std::runtime_error("cannot create play window");
        }
        if (primary) {
            SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(primary->window()));
            auto name = std::array<wchar_t, 256>{};
            GetWindowTextW(primary->window(), name.data(), static_cast<int>(name.size()));
            const auto title = std::wstring(name.data()) + L" · " + std::to_wstring(view_index + 1);
            SetWindowTextW(hwnd, title.c_str());
        }
        impl_->label =
            CreateWindowW(L"STATIC", L"", WS_CHILD | SS_CENTER, 0, 0, 960, 24, hwnd, nullptr, wc.hInstance, nullptr);
        auto settings = rengine::Settings{};
        settings.backend = "d3d12";
        settings.window = hwnd;
        settings.width = 960;
        settings.height = 580;
        settings.backend_directory = backends;
        impl_->renderer = Take(rengine::Renderer::Create(settings));
        if (primary) {
            Check(impl_->renderer->SetVSync(false));
        }
        auto material = rengine::DefaultMaterial();
        if (mode == GameMode::k3d) {
            material.depth_test = 1;
            material.depth_write = 1;
            material.lit = 1;
            const auto input = assets->Read("assets/sphere.gmesh", 4 * 1024 * 1024, impl_->metrics.assets);
            try {
                const auto mesh = DecodeMesh(input);
                const auto decoded = mesh.vertices.capacity() * sizeof(rengine::Vertex) +
                                     mesh.indices.capacity() * sizeof(std::uint32_t);
                impl_->metrics.assets.decode_peak_bytes = input.capacity() + decoded;
                impl_->sphere = Take(impl_->renderer->CreateMesh(
                    {mesh.vertices.data(), static_cast<std::uint32_t>(mesh.vertices.size()), mesh.indices.data(),
                     static_cast<std::uint32_t>(mesh.indices.size()), nullptr, 0}));
                impl_->metrics.assets.transfer_peak_bytes = input.capacity() + decoded;
                impl_->metrics.assets.peak_payload_bytes =
                    std::max(impl_->metrics.assets.peak_payload_bytes, impl_->metrics.assets.transfer_peak_bytes);
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string("assets/sphere.gmesh: ") + error.what());
            }
            auto grid_material = material;
            grid_material.lit = 0;
            impl_->grid_material = Take(impl_->renderer->CreateMaterial(grid_material));

            // 地面の格子でXZの移動範囲を示す。毎フレーム作り直さない。
            auto index = std::size_t{0};
            const auto add_vertex = [&](float x, float z) {
                auto& vertex = impl_->grid[index++];
                vertex.position[0] = x;
                vertex.position[2] = z;
                vertex.color[0] = 0.12f;
                vertex.color[1] = 0.22f;
                vertex.color[2] = 0.3f;
                vertex.color[3] = 1;
            };
            for (auto x = -8; x <= 8; ++x) {
                add_vertex(float(x), -6);
                add_vertex(float(x), 6);
            }
            for (auto z = -6; z <= 6; ++z) {
                add_vertex(-8, float(z));
                add_vertex(8, float(z));
            }
        }
        if (mode == GameMode::k2d) {
            const auto input = assets->Read("assets/sprite.png", 16 * 1024 * 1024, impl_->metrics.assets);
            try {
                const auto image = DecodePng(input);
                impl_->metrics.assets.decode_peak_bytes = input.capacity() + image.pixels.capacity();
                impl_->texture = Take(impl_->renderer->CreateTexture(
                    {image.width, image.height, 0, image.pixels.data(), image.pixels.size()}));
                material.texture = impl_->texture;
                material.blend = 1;
                impl_->metrics.assets.transfer_peak_bytes = input.capacity() + image.pixels.capacity();
                impl_->metrics.assets.peak_payload_bytes =
                    std::max(impl_->metrics.assets.peak_payload_bytes, impl_->metrics.assets.transfer_peak_bytes);
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string("assets/sprite.png: ") + error.what());
            }
        }
        impl_->material = Take(impl_->renderer->CreateMaterial(material));
        impl_->LoadVisuals(impl_->scene, *references, preview);
        if (preview == false && references->Exists("visual/game.json")) {
            if (mode != GameMode::k2d) {
                throw std::runtime_error("visual atlas currently requires a 2D game");
            }
            impl_->presentation_art =
                std::make_unique<GamePresentation>(*impl_->renderer, *references, impl_->metrics.assets);
            impl_->atlas_instances.reserve(8192 + impl_->game->actors().capacity());
            impl_->detail_instances.reserve(impl_->game->actors().capacity());
        }
        if (primary == nullptr && impl_->game != nullptr && impl_->game->HasUi()) {
            impl_->ui = std::make_unique<GameUi>(*impl_->renderer, *references, impl_->metrics.assets);
            impl_->ui->SetPresentation(impl_->presentation_art && impl_->presentation_art->HasLogo());
            if (impl_->ui->HasInputSettings() && impl_->game->HasProfile() == false) {
                throw std::runtime_error("input settings require game profile exports");
            }
            if (impl_->ui->HasDisplaySettings()) {
                impl_->display = std::make_unique<WindowDisplay>(hwnd, *impl_->renderer, parent == nullptr);
                impl_->ui->SetDisplay(impl_->display.get());
            }
            if (impl_->game->HasProfile()) {
                // 埋込みPIEは利用者保存を許可しない。単独Runnerの指定だけを受け付ける。
                if (parent != nullptr) {
                    profile_storage = {};
                }
                impl_->profile_store = std::make_unique<ProfileStore>(impl_->ui->profile_id(), profile_storage);
                const auto saved =
                    impl_->profile_store->Load(impl_->game->profile().schema, [this](const ProfileData& value) {
                        return impl_->game->RestoreProfile(value);
                    });
                if (saved) {
                    impl_->ui->SetLanguage(saved->language);
                    impl_->saved_audio = saved->audio;
                    impl_->input_router.SetSettings(saved->input);
                }
                if (impl_->display != nullptr) {
                    impl_->display->Restore(saved ? saved->display : DisplaySettings{});
                }
                impl_->submitted_profile =
                    SavedProfile{impl_->game->profile(), impl_->ui->language(),
                                 impl_->display ? impl_->display->confirmed() : DisplaySettings{}, impl_->saved_audio,
                                 impl_->input_router.settings()};
            }
            impl_->ui->SetInput(&impl_->input_router);
        }
        if (primary) {
            return;
        }
        if (preview) {
            if (mode == GameMode::k3d) {
                impl_->overlay_material = Take(impl_->renderer->CreateMaterial(rengine::DefaultMaterial()));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        impl_->audio = std::make_unique<GameAudio>(*assets, *references, impl_->scene, backends,
                                                   impl_->ui && impl_->ui->HasAudioSettings(), impl_->metrics.assets);
        impl_->audio->SetSettings(impl_->saved_audio);
        if (impl_->ui) {
            impl_->ui->SetAudio(impl_->audio.get());
        }
        impl_->metrics.audio_pcm_bytes = impl_->audio->Stats().pcm_bytes;
        impl_->clock.Start(hwnd);
        SetFocus(hwnd);
    }
    PlayWindow::~PlayWindow() = default;
    const FrameMetrics& PlayWindow::metrics() const
    {
        return impl_->metrics;
    }
    HWND PlayWindow::window() const
    {
        return impl_->window;
    }
    Game& PlayWindow::game()
    {
        if (impl_->game == nullptr) {
            throw std::logic_error("preview has no running VM");
        }
        return *impl_->game;
    }
    void PlayWindow::Reset(std::uint32_t preset)
    {
        impl_->Reset(preset);
    }
    void PlayWindow::Pause(bool paused)
    {
        impl_->Pause(paused);
    }
    void PlayWindow::UpdatePreviewScene(const SceneDocument& scene, std::optional<View2d> view, std::string selected)
    {
        if (impl_->game != nullptr || scene.game != impl_->scene.game) {
            throw std::logic_error("preview update requires the same dimension and no running VM");
        }
        ValidateScene(scene);
        if (view) {
            ValidateView2d(*view);
        }
        impl_->scene = scene;
        impl_->edit_view = view;
        impl_->edit_view_3d.reset();
        impl_->selected = std::move(selected);
        impl_->Draw();
    }
    void PlayWindow::UpdatePreviewScene3d(const SceneDocument& scene, View3d view, std::string selected)
    {
        if (impl_->game != nullptr || impl_->scene.game != "3d" || scene.game != "3d") {
            throw std::logic_error("3D preview update requires a stopped 3D scene");
        }
        ValidateScene(scene);
        ValidateView3d(view);
        impl_->scene = scene;
        impl_->edit_view.reset();
        impl_->edit_view_3d = view;
        impl_->selected = std::move(selected);
        impl_->Draw();
    }
    void PlayWindow::UpdatePreviewAssets(const SceneDocument& scene, const AssetSource& source, bool force)
    {
        if (impl_->game != nullptr || impl_->scene.game != scene.game) {
            throw std::logic_error("asset update requires a stopped preview with the same dimension");
        }
        ValidateScene(scene);
        impl_->LoadVisuals(scene, source, true, force);
    }

    std::vector<std::uint8_t> PlayWindow::Capture(std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0 || width > 3840 || height > 2160) {
            throw std::invalid_argument("capture dimensions must be within 3840 x 2160");
        }
        if (PerformanceActive()) {
            throw std::logic_error("stop performance capture before GPU image readback");
        }
        impl_->SyncWindows();
        // 画像の取得ではゲーム時間を進めない。読み戻した後に描画先を解放する。
        auto pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4);
        const auto target = Take(impl_->renderer->CreateTarget({width, height}));
        try {
            impl_->Draw(target, width, height);
            Check(impl_->renderer->ReadTarget(target, pixels));
        } catch (...) {
            // 読み戻しの失敗を伝えるため、後片付けの結果で元の例外を上書きしない。
            static_cast<void>(impl_->renderer->DestroyTarget(target));
            throw;
        }
        Check(impl_->renderer->DestroyTarget(target));
        return EncodePng(pixels, width, height);
    }

    std::vector<HWND> PlayWindow::ViewWindows() const
    {
        auto result = std::vector<HWND>{impl_->window};
        if (impl_->temporary_windows) {
            for (const auto& view : impl_->satellites) {
                result.push_back(view->window());
            }
        }
        return result;
    }

    std::vector<std::uint8_t> PlayWindow::CaptureView(std::uint32_t index, std::uint32_t width, std::uint32_t height)
    {
        if (index == 0) {
            return Capture(width, height);
        }
        impl_->SyncWindows();
        if (impl_->temporary_windows == false || index > impl_->satellites.size()) {
            throw std::invalid_argument("linked view is not active");
        }
        return impl_->satellites[index - 1]->Capture(width, height);
    }
    void PlayWindow::StartPerformance(PerformanceConfig config)
    {
        if (impl_->game == nullptr || impl_->game->state().phase == Phase::kFaulted) {
            throw std::logic_error("performance capture requires non-faulted play");
        }
        if (impl_->performance.active()) {
            throw std::logic_error("performance capture already active");
        }
        const auto presentation = impl_->renderer->HasPresentationTiming()
                                      ? Take(impl_->renderer->ReadPresentationTiming())
                                      : rengine::PresentationTiming{};
        impl_->performance.Start(config, impl_->embedded ? "EditorPIE" : "GameRunner", impl_->scene.game);
        impl_->captured_counters.clear();
        impl_->phase_frames = {};
        impl_->captured_ticks = 0;
        impl_->paused_frames = 0;
        impl_->presentation = presentation;
        impl_->presentation_queries = 0;
        impl_->presentation_unavailable = 0;
        impl_->presentation_disjoint = 0;
        impl_->presentation_duplicates = 0;
        impl_->presentation_gaps = 0;
    }
    void PlayWindow::StopPerformance()
    {
        if (impl_->performance.active()) {
            impl_->performance.Stop();
            impl_->captured_counters = impl_->Counters();
        }
    }
    bool PlayWindow::PerformanceActive() const
    {
        return impl_->performance.active();
    }
    PerformanceReport PlayWindow::Performance()
    {
        // ウィンドウ手続きの失敗処理から確保やOS照会を行わず、照会時に部分結果を確定する。
        if (impl_->game && impl_->game->state().phase == Phase::kFaulted) {
            StopPerformance();
        }
        auto report = impl_->performance.Report();
        const auto counters = impl_->performance.active() ? impl_->Counters() : impl_->captured_counters;
        report.counters.insert(report.counters.end(), counters.begin(), counters.end());
        return report;
    }
    void PlayWindow::RecordHostTime(Timing timing, std::uint64_t ns)
    {
        impl_->performance.Host(timing, ns);
    }
} // namespace gengine
