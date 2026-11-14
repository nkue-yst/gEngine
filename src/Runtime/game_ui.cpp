#include "game_ui.hpp"
#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <windows.h>

namespace gengine
{
    namespace
    {
        void Require(bool valid, const char* message)
        {
            if (valid == false) {
                throw std::runtime_error(message);
            }
        }

        std::wstring Text(const std::string& text)
        {
            Require(text.size() <= 8192, "UI text exceeds limit");
            const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                   static_cast<int>(text.size()), nullptr, 0);
            Require(count > 0, "UI text requires nonempty UTF-8");
            auto result = std::wstring(static_cast<std::size_t>(count), L' ');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                result.data(), count);
            Require(std::none_of(result.begin(), result.end(),
                                 [](wchar_t c) {
                                     return (c < 32 && c != L'\n') || (c >= 0xd800 && c <= 0xdfff);
                                 }),
                    "UI atlas currently supports printable BMP characters and newline");
            return result;
        }
    } // namespace

    struct GameUi::Impl
    {
        rengine::Renderer& renderer;
        rengine::Texture texture{};
        rengine::Material material{};
        std::map<wchar_t, std::uint32_t> glyphs;
        std::vector<float> advances;
        std::array<std::map<std::string, std::wstring>, 2> messages;
        std::array<std::vector<std::wstring>, 2> options;
        std::array<std::vector<std::wstring>, 2> status;
        std::string profile_id;
        bool display_settings = false;
        WindowDisplay* display = nullptr;
        bool audio_settings = false;
        GameAudio* audio = nullptr;
        bool input_settings = false;
        InputRouter* input = nullptr;
        std::optional<std::uint32_t> capture_key;
        std::optional<std::uint32_t> capture_button;
        bool binding_invalid = false;
        std::uint64_t choice_request = 0;
        std::uint32_t choice_selected = 0;
        float row_step = .64f;
        float menu_left = .55f;
        float menu_right = 15.45f;
        float menu_top = .95f;
        bool presentation = false;
        bool previous_title = false;
        PresentationState previous_presentation;
        bool result_details = false;
        bool profile_reset = false;
        bool reset_permitted = false;
        std::vector<std::uint32_t> result_status;
        std::uint32_t balance_slot = 0;
        std::uint32_t reward_slot = 0;
        std::vector<std::uint32_t> record_slots;
        std::vector<ProfileAction> workshop_actions;
        std::array<std::vector<std::wstring>, 2> upgrades;
        std::array<std::vector<std::wstring>, 2> records;
        enum class Page
        {
            kTitle,
            kWorkshop,
            kRecords,
            kDisplay,
            kAudio,
            kGuide,
            kCredits,
            kSettings,
            kControls,
            kKeyboard,
            kGamepad,
            kPause,
            kConfirmQuit,
            kConfirmAbandon,
            kConfirmReset
        };
        Page page = Page::kTitle;
        Page confirm_return = Page::kTitle;
        std::uint32_t selected = 0;
        std::uint32_t menu_count = 0;
        std::uint32_t first_line = 0;
        std::uint32_t language = 0;
        std::uint32_t columns = 0;
        std::uint32_t rows = 0;
        std::vector<rengine::Instance> instances;
        std::wstring previous;
        struct Hit
        {
            float x;
            float y;
            float width;
            float height;
            std::uint32_t row;
            std::uint32_t key = VK_RETURN;
            bool slider = false;
            bool Contains(float px, float py) const
            {
                return px >= x && px <= x + width && py >= y && py <= y + height;
            }
        };
        std::vector<Hit> hits;
        std::optional<Hit> drag;

        // 単色セルと文字を同じatlasで描き、解像度に依存しない16:9座標を使う。
        void Box(float x, float y, float width, float height, std::array<float, 4> color)
        {
            auto box = rengine::Sprite(x, y, width, height);
            box.uv_rect[0] = .5f / float(columns * 48);
            box.uv_rect[1] = .5f / float(rows * 64);
            box.uv_rect[2] = box.uv_rect[3] = 0;
            std::copy(color.begin(), color.end(), box.color);
            instances.push_back(box);
        }

        float Label(const std::wstring& text, float x, float y, float width, float size = .32f,
                    std::array<float, 4> color = {.84f, .9f, .92f, 1.f})
        {
            Require(std::all_of(text.begin(), text.end(),
                                [&](auto character) {
                                    return character == L'\n' || glyphs.contains(character);
                                }),
                    "UI text contains a missing glyph");
            const auto scale = size / 40.f;
            const auto origin = x;
            // 英単語はまとまりで折り返し、日本語は字形単位で領域内へ収める。
            for (auto index = std::size_t{0}; index < text.size(); ++index) {
                const auto character = text[index];
                if (character == L'\n') {
                    x = origin;
                    y += size * 1.6f;
                    continue;
                }
                const auto cell = glyphs.at(character);
                const auto advance = advances[cell - 1] * scale;
                auto word_width = advance;
                if (character > 32 && character < 127 && (index == 0 || text[index - 1] == L' ')) {
                    for (auto next = index + 1; next < text.size() && text[next] > 32 && text[next] < 127; ++next) {
                        word_width += advances[glyphs.at(text[next]) - 1] * scale;
                    }
                }
                if (x > origin && x + std::min(word_width, width) > origin + width) {
                    x = origin;
                    y += size * 1.6f;
                }
                auto glyph = rengine::Sprite(x, y, 48 * scale, 64 * scale);
                glyph.uv_rect[0] = (float(cell % columns) * 48 + .5f) / float(columns * 48);
                glyph.uv_rect[1] = (float(cell / columns) * 64 + .5f) / float(rows * 64);
                glyph.uv_rect[2] = 47.f / float(columns * 48);
                glyph.uv_rect[3] = 63.f / float(rows * 64);
                std::copy(color.begin(), color.end(), glyph.color);
                instances.push_back(glyph);
                x += advance;
            }
            return y + size * 1.6f;
        }

        // 透ける面の上端だけに薄い光を重ねる。外周の全周枠で画面を分断しない。
        void Glass(float x, float y, float width, float height, bool focused = false)
        {
            Box(x - .025f, y + .04f, width + .05f, height, {0, .008f, .016f, .18f});
            Box(x, y, width, height,
                focused ? std::array{.055f, .14f, .17f, .78f} : std::array{.012f, .034f, .049f, .75f});
            for (auto band = 0; band < 6; ++band) {
                Box(x, y + height * float(band) / 6, width, height / 6,
                    {.3f, .52f, .56f, (focused ? .07f : .035f) * (1.f - float(band) / 6)});
            }
            Box(x + .12f, y, width - .24f, .012f, {.71f, .79f, .74f, focused ? .55f : .22f});
        }

        void Button(const std::wstring& label, Hit hit, bool focused, bool enabled = true)
        {
            Glass(hit.x, hit.y, hit.width, hit.height, focused && enabled);
            if (focused && enabled) {
                Box(hit.x, hit.y, .035f, hit.height, {.8f, .69f, .43f, 1.f});
            }
            Label(label, hit.x + .22f, hit.y + .09f, hit.width - .44f, .29f,
                  enabled ? std::array{.88f, .93f, .93f, 1.f} : std::array{.36f, .43f, .47f, 1.f});
            if (enabled) {
                hits.push_back(hit);
            }
        }

        void MenuLayout(const std::wstring& text, bool modal, bool title, const Game& game, const std::wstring& notice)
        {
            const auto& scene = game.state().presentation;
            if (text == previous && title == previous_title && scene.scene == previous_presentation.scene &&
                scene.cue == previous_presentation.cue && scene.cue_seconds == previous_presentation.cue_seconds) {
                return;
            }
            Require(text.size() < 2048, "UI frame exceeds glyph limit");
            instances.clear();
            hits.clear();
            const auto& state = game.state();
            const auto choosing =
                state.phase == Phase::kPlaying && state.paused == false && game.interaction().request != 0;
            if (modal == false) {
                // 戦闘中は必要な状態だけを上辺へ置き、設定や操作説明を持ち込まない。
                Glass(0, 0, 16, .72f);
                Label(L"HP  " + std::to_wstring(state.hp), .35f, .12f, 1.7f, .3f, {.7f, .95f, .87f, 1.f});
                auto x = 2.2f;
                for (auto index = 0u; index < state.status_count; ++index) {
                    const auto& value = state.status[index];
                    const auto width = 12.9f / float(std::max(1u, state.status_count));
                    Label(status[language][value.label] + L" " + std::to_wstring(value.value), x, .14f, width - .15f,
                          .25f);
                    if (value.maximum > 0) {
                        Box(x, .57f, width - .25f, .035f, {.12f, .22f, .27f, 1.f});
                        Box(x, .57f, (width - .25f) * std::clamp(float(value.value) / float(value.maximum), 0.f, 1.f),
                            .035f, {.38f, .73f, .72f, 1.f});
                    }
                    x += width;
                }
                // ポーズの入口もポインターから使えるようにする。
                Box(15.25f, .08f, .55f, .5f, {.025f, .065f, .09f, .86f});
                Box(15.42f, .2f, .065f, .24f, {.84f, .9f, .92f, 1.f});
                Box(15.56f, .2f, .065f, .24f, {.84f, .9f, .92f, 1.f});
                hits.push_back({15.25f, .08f, .55f, .5f, 0, VK_ESCAPE});
                const auto scene_label = messages[language].find("scene." + std::to_string(scene.scene));
                if (scene_label != messages[language].end()) {
                    Label(scene_label->second, .32f, 8.55f, 11.f, .22f, {.76f, .79f, .75f, .85f});
                }
                const auto cue_label = messages[language].find("cue." + std::to_string(scene.cue));
                if (scene.cue_seconds > 0 && cue_label != messages[language].end()) {
                    const auto opacity = std::min(1.f, scene.cue_seconds / .6f);
                    Box(3.4f, 1.4f, 9.2f, 1.12f, {.012f, .038f, .055f, .72f * opacity});
                    Box(5.3f, 1.4f, 5.4f, .015f, {.86f, .74f, .48f, .8f * opacity});
                    Label(cue_label->second, 3.8f, 1.63f, 8.4f, .38f, {.96f, .87f, .68f, opacity});
                }
            }
            else {
                auto lines = std::vector<std::wstring>{};
                auto start = std::size_t{0};
                while (start < text.size()) {
                    const auto end = text.find(L'\n', start);
                    lines.push_back(text.substr(start, end == std::wstring::npos ? end : end - start));
                    if (end == std::wstring::npos) {
                        break;
                    }
                    start = end + 1;
                }
                const auto compact = page == Page::kPause || page == Page::kConfirmQuit ||
                                     page == Page::kConfirmAbandon || page == Page::kConfirmReset;
                const auto left = compact ? 4.65f : 1.85f;
                const auto content_width = compact ? 6.7f : 12.3f;
                if (title == false) {
                    Box(0, 0, 16, 9, {.003f, .01f, .02f, .36f});
                    Glass(left - .6f, .65f, content_width + 1.2f, 7.7f);
                    if (compact == false || page == Page::kPause) {
                        Box(left, 1.62f, content_width, .015f, {.43f, .38f, .26f, .8f});
                    }
                }
                auto body_y = 1.9f;
                for (auto index = 0u; index < first_line && index < lines.size(); ++index) {
                    if (lines[index].empty()) {
                        continue;
                    }
                    if (index == 0) {
                        body_y = Label(lines[index], left, .96f, content_width, compact ? .34f : .4f,
                                       {.9f, .83f, .64f, 1.f});
                        body_y = std::max(body_y + .32f, 1.95f);
                    }
                    else {
                        body_y = Label(lines[index], left + .1f, body_y, content_width - .2f, .27f) + .12f;
                    }
                }
                const auto fields = page == Page::kDisplay || page == Page::kAudio || page == Page::kKeyboard ||
                                    page == Page::kGamepad || page == Page::kSettings;
                auto item_y = title ? 3.f : std::max(2.f, body_y + .15f);
                const auto bindings = page == Page::kKeyboard || page == Page::kGamepad;
                const auto step = title ? .57f : bindings && menu_count > 8 ? .64f : fields ? .78f : .75f;
                for (auto index = 0u; index < menu_count && first_line + index < lines.size(); ++index) {
                    const auto item = lines[first_line + index].substr(2);
                    const auto focus = index == (choosing ? choice_selected : selected);
                    const auto divider = item.find(L" : ");
                    const auto label = item.substr(0, divider);
                    const auto value = divider == std::wstring::npos ? L"" : item.substr(divider + 3);
                    const auto pending = page == Page::kDisplay && display != nullptr && display->pending();
                    const auto slider = page == Page::kAudio && index < 3;
                    const auto cycle = (page == Page::kDisplay && pending == false && index < 3) ||
                                       (page == Page::kSettings && index == 3);
                    const auto toggle = (page == Page::kAudio && index == 3) ||
                                        (page == Page::kDisplay && pending == false && index == 3);
                    const auto binding =
                        (page == Page::kKeyboard || page == Page::kGamepad) && divider != std::wstring::npos;
                    const auto back = item == messages[language].at("return");
                    const auto display_disabled = page == Page::kDisplay && pending == false && index < 5 &&
                                                  (display == nullptr || display->enabled() == false);
                    const auto resolution_locked = page == Page::kDisplay && index == 1 && display != nullptr &&
                                                   display->draft().mode == DisplayMode::kBorderless;
                    if (choosing) {
                        // 強化は効果を読み比べるカードに分け、カード全体をクリック領域にする。
                        const auto card_left = 1.85f + float(index) * 4.18f;
                        const auto hit = Hit{card_left, 2.35f, 3.94f, 4.25f, index};
                        Glass(card_left, hit.y, hit.width, hit.height, focus);
                        Box(card_left, hit.y, hit.width, .035f, {.7f, .61f, .39f, 1.f});
                        const auto split = item.find(L"  /  ");
                        Label(item.substr(0, split), card_left + .28f, 2.7f, 3.38f, .34f, {.92f, .85f, .66f, 1.f});
                        if (split != std::wstring::npos) {
                            Label(item.substr(split + 5), card_left + .28f, 3.9f, 3.38f, .28f);
                        }
                        hits.push_back(hit);
                        continue;
                    }
                    if (back) {
                        Button(L"<  " + item, {1.95f, 7.35f, 3.6f, .62f, index}, focus);
                        continue;
                    }
                    if (bindings && item == messages[language].at("defaults")) {
                        Button(item, {8.4f, 7.35f, 5.6f, .62f, index}, focus);
                        continue;
                    }
                    if (page == Page::kWorkshop && index < workshop_actions.size()) {
                        // 名前・効果と購入操作を分け、資金不足や最大段階では購入部を無効にする。
                        const auto& action = workshop_actions[index];
                        const auto& description = upgrades[language][action.label];
                        const auto split = description.find(L" / ");
                        Box(1.95f, item_y, 12.1f, 1.02f, {.025f, .065f, .09f, .86f});
                        Label(description.substr(0, split), 2.2f, item_y + .08f, 6.8f, .31f, {.9f, .83f, .64f, 1.f});
                        if (split != std::wstring::npos) {
                            Label(description.substr(split + 3), 2.2f, item_y + .53f, 6.8f, .25f);
                        }
                        Label(std::to_wstring(action.rank) + L" / " + std::to_wstring(action.maximum), 9.25f,
                              item_y + .1f, 1.7f, .27f);
                        Label(messages[language].at("cost") + L" " + std::to_wstring(action.cost), 9.25f, item_y + .54f,
                              1.7f, .23f);
                        const auto enabled = action.enabled && action.rank < action.maximum;
                        Button(messages[language].at(action.rank == action.maximum ? "capped"
                                                     : enabled                     ? "purchase"
                                                                                   : "locked"),
                               {11.15f, item_y + .19f, 2.65f, .64f, index}, focus, enabled);
                        item_y += 1.15f;
                        continue;
                    }
                    if (slider || cycle || toggle || binding) {
                        if (focus) {
                            Box(1.85f, item_y, 12.3f, .65f, {.045f, .095f, .12f, .8f});
                        }
                        Label(label, 2.05f, item_y + .11f, 5.4f, .3f);
                        if (slider) {
                            const auto settings = audio ? audio->settings() : AudioSettings{};
                            const auto volume = index == 0   ? settings.master
                                                : index == 1 ? settings.music
                                                             : settings.effects;
                            const auto position = 8.1f + 4.35f * float(volume) / 100.f;
                            Box(8.1f, item_y + .29f, 4.35f, .055f, {.12f, .24f, .29f, 1.f});
                            Box(8.1f, item_y + .29f, position - 8.1f, .055f, {.38f, .78f, .76f, 1.f});
                            Box(position - .055f, item_y + .19f, .11f, .25f, {.91f, .85f, .65f, 1.f});
                            Label(value, 12.9f, item_y + .1f, 1.05f, .28f);
                            if (audio != nullptr) {
                                hits.push_back({8.1f, item_y, 4.35f, .65f, index, VK_RETURN, true});
                            }
                        }
                        else if (cycle) {
                            const auto enabled = display_disabled == false && resolution_locked == false;
                            Button(L"<", {7.7f, item_y, .66f, .62f, index, VK_LEFT}, false, enabled);
                            Label(value, 8.6f, item_y + .12f, 4.2f, .28f,
                                  enabled ? std::array{.83f, .91f, .93f, 1.f} : std::array{.36f, .43f, .47f, 1.f});
                            Button(L">", {13.35f, item_y, .66f, .62f, index, VK_RIGHT}, false, enabled);
                        }
                        else {
                            Button(value, {10.1f, item_y, 3.9f, .62f, index}, focus, display_disabled == false);
                            if (toggle) {
                                const auto on = value == messages[language].at("on");
                                Box(13.37f, item_y + .17f, .4f, .25f,
                                    on ? std::array{.3f, .75f, .65f, 1.f} : std::array{.18f, .25f, .29f, 1.f});
                                Box(on ? 13.6f : 13.4f, item_y + .2f, .14f, .19f, {.87f, .91f, .9f, 1.f});
                            }
                        }
                    }
                    else {
                        const auto width = title ? 5.7f : compact ? 6.5f : 7.4f;
                        const auto height = title ? .5f : .62f;
                        Button(item, {title ? 1.05f : left + .1f, item_y, width, height, index}, focus,
                               display_disabled == false);
                    }
                    item_y += step;
                }
                if (Capturing()) {
                    // 割当て待ちをマウスだけでも取り消せる。背後の設定行は反応させない。
                    hits.clear();
                    Box(0, 0, 16, 9, {.003f, .01f, .02f, .8f});
                    Box(3.3f, 2.6f, 9.4f, 3.5f, {.02f, .06f, .09f, 1.f});
                    Label(messages[language].at(binding_invalid ? "binding_invalid"
                                                : capture_key   ? "press_key"
                                                                : "press_button"),
                          3.9f, 3.1f, 8.2f, .32f);
                    Button(messages[language].at("return"), {8.6f, 5.f, 3.5f, .62f, selected, VK_ESCAPE}, true);
                }
                if (notice.empty() == false) {
                    Label(notice, 1.95f, 8.48f, 12.1f, .22f, {.89f, .71f, .47f, 1.f});
                }
            }
            previous = text;
            previous_title = title;
            previous_presentation = scene;
        }

        bool Capturing() const
        {
            return capture_key.has_value() || capture_button.has_value();
        }

        explicit Impl(rengine::Renderer& value) : renderer(value)
        {
        }
        ~Impl()
        {
            // マテリアルの破棄結果にかかわらず、テクスチャの解放にも進む。
            if (material.id != 0) {
                static_cast<void>(renderer.DestroyMaterial(material));
            }
            if (texture.id != 0) {
                static_cast<void>(renderer.DestroyTexture(texture));
            }
        }

        void Layout(const std::wstring& text, bool modal, bool title)
        {
            if (text == previous && title == previous_title) {
                return;
            }
            Require(text.size() < 2048, "UI frame exceeds glyph limit");
            instances.clear();
            // atlasの先頭セルは単色。文字と同じ一括描画で背景を敷く。
            auto panel = rengine::Sprite(modal ? .55f : 0, modal ? .65f : 0, modal ? 14.9f : 16, modal ? 7.7f : .65f);
            panel.uv_rect[0] = .5f / float(columns * 48);
            panel.uv_rect[1] = .5f / float(rows * 64);
            panel.uv_rect[2] = panel.uv_rect[3] = 0;
            panel.color[0] = .004f;
            panel.color[1] = .012f;
            panel.color[2] = .018f;
            panel.color[3] = modal ? .94f : .86f;
            if (title == false) {
                instances.push_back(panel);
            }

            const auto line_breaks = static_cast<float>(std::count(text.begin(), text.end(), L'\n'));
            row_step = title ? .55f : modal ? std::min(.64f, 6.85f / std::max(1.f, line_breaks)) : .64f;
            menu_left = title ? .95f : .55f;
            menu_right = title ? 6.65f : 15.45f;
            menu_top = title ? 3.f : .95f;
            auto y = modal ? menu_top : .1f;
            auto row = 0u;
            auto start = std::size_t{0};
            while (start < text.size()) {
                const auto end = text.find(L'\n', start);
                const auto line = text.substr(start, end == std::wstring::npos ? end : end - start);
                auto width = 0.f;
                for (const auto character : line) {
                    const auto found = glyphs.find(character);
                    Require(found != glyphs.end(), "UI text contains a missing glyph");
                    width += advances[found->second - 1];
                }
                // タイトルは背景の余白へ寄せ、操作案内は下辺にまとめて文字幅を確保する。
                const auto title_item = title && row < menu_count;
                const auto footer = title && title_item == false;
                if (footer) {
                    y = 7.2f + float(row - menu_count) * .42f;
                }
                const auto scale = std::min((footer ? .42f : row_step) / .64f / 120.f,
                                            (title_item ? 5.2f : 14.f) / std::max(width, 1.f));
                auto x = title_item ? 1.15f : (16.f - width * scale) * .5f;
                if (line.starts_with(L"> ")) {
                    auto highlight = panel;
                    highlight.world[12] = title_item ? .95f : std::max(.7f, x - .22f);
                    highlight.world[13] = y - .03f;
                    highlight.world[0] = title_item ? 5.7f : std::min(14.6f, width * scale + .44f);
                    highlight.world[5] = row_step;
                    highlight.color[0] = .2f;
                    highlight.color[1] = .42f;
                    highlight.color[2] = .48f;
                    highlight.color[3] = .32f;
                    instances.push_back(highlight);
                }
                for (const auto character : line) {
                    const auto cell = glyphs.at(character);
                    auto glyph = rengine::Sprite(x, y, 48 * scale, 64 * scale);
                    // 補間が隣のセルを拾わないよう、端のtexel中心へ内側から合わせる。
                    glyph.uv_rect[0] = (float(cell % columns) * 48 + .5f) / float(columns * 48);
                    glyph.uv_rect[1] = (float(cell / columns) * 64 + .5f) / float(rows * 64);
                    glyph.uv_rect[2] = 47.f / float(columns * 48);
                    glyph.uv_rect[3] = 63.f / float(rows * 64);
                    glyph.color[0] = line.starts_with(L"> ") ? 1.f : .8f;
                    glyph.color[1] = .94f;
                    instances.push_back(glyph);
                    x += advances[cell - 1] * scale;
                }
                y += row_step;
                ++row;
                if (end == std::wstring::npos) {
                    break;
                }
                start = end + 1;
            }
            previous = text;
            previous_title = title;
        }
    };

    GameUi::GameUi(rengine::Renderer& renderer, const AssetSource& source, AssetMetrics& metrics)
        : impl_(std::make_unique<Impl>(renderer))
    {
        // 実装とatlasの形式を先に照合し、破損したUIを黙って標準表示へ置き換えない。
        const auto bytes = source.Read("ui/game.json", 128 * 1024, metrics);
        const auto data = nlohmann::json::parse(bytes);
        Require(data.at("version") == 1, "unsupported game UI version");
        const auto characters = Text(data.at("glyphs").get<std::string>());
        Require(characters.size() <= 1023, "UI atlas exceeds glyph limit");
        impl_->columns = 32;
        impl_->rows = static_cast<std::uint32_t>((characters.size() + 1 + 31) / 32);
        impl_->advances = data.at("advances").get<std::vector<float>>();
        Require(impl_->advances.size() == characters.size(), "UI advance count differs");
        for (auto index = std::size_t{0}; index < characters.size(); ++index) {
            Require(impl_->glyphs.emplace(characters[index], static_cast<std::uint32_t>(index + 1)).second,
                    "UI atlas has duplicate glyphs");
            Require(impl_->advances[index] > 0 && impl_->advances[index] <= 48, "invalid UI glyph advance");
        }
        const auto display_version = data.value("display_settings", 0u);
        Require(display_version <= 1, "unsupported display settings UI version");
        impl_->display_settings = display_version == 1;
        const auto audio_version = data.value("audio_settings", 0u);
        Require(audio_version <= 1 && (audio_version == 0 || impl_->display_settings),
                "audio settings require display settings UI");
        impl_->audio_settings = audio_version == 1;
        const auto input_version = data.value("input_settings", 0u);
        Require(input_version <= 1 && (input_version == 0 || impl_->audio_settings),
                "input settings require audio/display settings UI");
        impl_->input_settings = input_version == 1;
        impl_->result_details = data.value("result_details", false);
        impl_->profile_reset = data.value("profile_reset", false);
        Require(impl_->profile_reset == false || impl_->input_settings, "profile reset requires input settings UI");
        impl_->result_status = data.value("result_status", std::vector<std::uint32_t>{});
        Require(impl_->result_status.size() <= 4 &&
                    std::all_of(impl_->result_status.begin(), impl_->result_status.end(),
                                [](auto index) {
                                    return index < 4;
                                }),
                "invalid result status slots");
        if (data.contains("profile")) {
            const auto& profile = data.at("profile");
            impl_->profile_id = profile.at("id").get<std::string>();
            impl_->balance_slot = profile.at("balance_slot").get<std::uint32_t>();
            impl_->reward_slot = profile.at("reward_slot").get<std::uint32_t>();
            impl_->record_slots = profile.at("record_slots").get<std::vector<std::uint32_t>>();
            Require(impl_->balance_slot < 32 && impl_->reward_slot < 32 && impl_->record_slots.size() <= 4 &&
                        std::all_of(
                            impl_->record_slots.begin(), impl_->record_slots.end(),
                            [](auto slot) {
                                return slot < 32;
                            }),
                    "invalid profile UI slots");
        }
        Require(impl_->input_settings == false || impl_->profile_id.empty() == false,
                "input settings require profile UI metadata");
        const auto languages = std::array{"ja", "en"};
        for (auto index = std::size_t{0}; index < languages.size(); ++index) {
            const auto& localized = data.at("languages").at(languages[index]);
            for (const auto* prefix : {"scene.", "cue."}) {
                for (auto number = 1u; number <= (std::string_view(prefix) == "scene." ? 16u : 64u); ++number) {
                    const auto key = prefix + std::to_string(number);
                    if (localized.contains(key)) {
                        impl_->messages[index][key] = Text(localized.at(key).get<std::string>());
                    }
                }
            }
            for (const auto* key : {"title", "start", "controls", "choose", "paused", "clear", "fail", "back"}) {
                impl_->messages[index][key] = Text(localized.at(key).get<std::string>());
            }
            impl_->messages[index]["paused_controls"] =
                Text(localized.value("paused_controls", localized.at("back").get<std::string>()));
            if (impl_->result_details) {
                for (const auto* key : {"elapsed", "remaining_hp"}) {
                    impl_->messages[index][key] = Text(localized.at(key).get<std::string>());
                }
            }
            if (impl_->profile_reset) {
                for (const auto* key : {"reset_progress", "confirm_reset"}) {
                    impl_->messages[index][key] = Text(localized.at(key).get<std::string>());
                }
            }
            if (impl_->profile_id.empty() == false) {
                for (const auto* key : {"play", "workshop", "records", "guide", "credits", "quit", "return", "balance",
                                        "cost", "locked", "capped", "navigation", "preview", "recovered", "protected",
                                        "save_failed", "reward", "guide_text", "credits_text"}) {
                    impl_->messages[index][key] = Text(localized.at(key).get<std::string>());
                }
                for (const auto& label : localized.at("upgrades")) {
                    impl_->upgrades[index].push_back(Text(label.get<std::string>()));
                }
                impl_->messages[index]["purchase"] =
                    Text(localized.value("purchase", localized.at("cost").get<std::string>()));
                for (const auto& label : localized.at("records_labels")) {
                    impl_->records[index].push_back(Text(label.get<std::string>()));
                }
                Require(impl_->upgrades[index].size() <= 256 &&
                            impl_->records[index].size() == impl_->record_slots.size(),
                        "invalid profile UI labels");
            }
            if (impl_->display_settings) {
                for (const auto* key :
                     {"settings", "display_mode", "windowed", "borderless", "fullscreen", "resolution",
                      "desktop_resolution", "monitor", "vsync", "on", "off", "apply", "keep", "cancel",
                      "confirm_display", "display_failed", "display_unavailable", "settings_navigation"}) {
                    impl_->messages[index][key] = Text(localized.at(key).get<std::string>());
                }
            }
            if (impl_->input_settings) {
                for (const auto* key : {"display",
                                        "controls_settings",
                                        "keyboard",
                                        "controller",
                                        "language",
                                        "defaults",
                                        "press_key",
                                        "press_button",
                                        "binding_invalid",
                                        "resume",
                                        "return_title",
                                        "retry",
                                        "confirm_quit",
                                        "confirm_abandon",
                                        "yes",
                                        "no",
                                        "pad_unavailable",
                                        "menu_navigation_pad",
                                        "move_left",
                                        "move_right",
                                        "move_up",
                                        "move_down",
                                        "focus_action",
                                        "pause_action",
                                        "descend_action",
                                        "ascend_action",
                                        "choice_navigation",
                                        "choice_navigation_pad"}) {
                    impl_->messages[index][key] = Text(localized.at(key).get<std::string>());
                }
            }
            const auto& options = localized.at("options");
            if (impl_->audio_settings) {
                for (const auto* key :
                     {"audio", "master", "music", "effects", "muted", "test_audio", "audio_unavailable"}) {
                    impl_->messages[index][key] = Text(localized.at(key).get<std::string>());
                }
            }
            Require(options.is_array() && options.size() <= 256, "invalid UI options");
            for (const auto& option : options) {
                impl_->options[index].push_back(Text(option.get<std::string>()));
            }
            if (localized.contains("status")) {
                const auto& labels = localized.at("status");
                Require(labels.is_array() && labels.size() <= 256, "invalid UI status labels");
                for (const auto& label : labels) {
                    impl_->status[index].push_back(Text(label.get<std::string>()));
                }
            }
        }
        const auto image = DecodePng(source.Read("ui/font.png", 16 * 1024 * 1024, metrics));
        Require(image.width == impl_->columns * 48 && image.height == impl_->rows * 64, "UI atlas dimensions differ");
        const auto texture =
            renderer.CreateTexture({image.width, image.height, 0, image.pixels.data(), image.pixels.size()});
        Require(texture.has_value(), "cannot create UI texture");
        impl_->texture = *texture;
        auto description = rengine::DefaultMaterial();
        description.texture = impl_->texture;
        // 通常PNGのアルファ乗算はrEngineのシェーダーが行う。文字セルの空白を透過させる。
        description.blend = 1;
        const auto material = renderer.CreateMaterial(description);
        Require(material.has_value(), "cannot create UI material");
        impl_->material = *material;
        impl_->instances.reserve(2048);
    }

    GameUi::~GameUi() = default;

    void GameUi::SetPresentation(bool enabled)
    {
        impl_->presentation = enabled;
        impl_->previous.clear();
    }

    bool GameUi::TitleMenuVisible(const Game& game) const
    {
        return impl_->presentation && game.HasProfile() && game.state().phase == Phase::kStart &&
               impl_->page == Impl::Page::kTitle;
    }

    void GameUi::SwitchLanguage()
    {
        impl_->language = 1 - impl_->language;
        impl_->previous.clear();
    }

    void GameUi::SetLanguage(std::uint32_t language)
    {
        Require(language < 2, "invalid UI language");
        impl_->language = language;
        impl_->previous.clear();
    }
    std::uint32_t GameUi::language() const
    {
        return impl_->language;
    }
    const std::string& GameUi::profile_id() const
    {
        return impl_->profile_id;
    }
    bool GameUi::HasDisplaySettings() const
    {
        return impl_->display_settings;
    }
    void GameUi::SetDisplay(WindowDisplay* display)
    {
        impl_->display = display;
    }
    bool GameUi::HasAudioSettings() const
    {
        return impl_->audio_settings;
    }
    void GameUi::SetAudio(GameAudio* audio)
    {
        impl_->audio = audio;
    }
    bool GameUi::HasInputSettings() const
    {
        return impl_->input_settings;
    }
    void GameUi::SetInput(InputRouter* input)
    {
        impl_->input = input;
    }
    bool GameUi::CapturingInput() const
    {
        return impl_->capture_key.has_value() || impl_->capture_button.has_value();
    }
    bool GameUi::PadBinding(std::uint32_t button)
    {
        impl_->previous.clear();
        if (impl_->capture_button && impl_->input != nullptr) {
            if (impl_->input->BindButton(*impl_->capture_button, button)) {
                impl_->capture_button.reset();
                impl_->binding_invalid = false;
            }
            else {
                impl_->binding_invalid = true;
            }
            return true;
        }
        return false;
    }
    void GameUi::ShowPause()
    {
        if (impl_->input_settings && impl_->page == Impl::Page::kTitle) {
            impl_->page = Impl::Page::kPause;
            impl_->selected = 0;
        }
    }
    UiCommand GameUi::TogglePause(Game& game)
    {
        if (game.state().phase == Phase::kPlaying && game.state().paused == false) {
            return UiCommand::kPause;
        }
        return game.state().paused && impl_->page == Impl::Page::kPause ? UiCommand::kResume : UiCommand::kHandled;
    }
    bool GameUi::RequestClose(Game& game)
    {
        if (impl_->input_settings == false || game.state().phase == Phase::kFaulted) {
            return false;
        }
        // 終了確認で入力待ちを中断し、取消し後も元のページへ戻れるようにする。
        if (impl_->page != Impl::Page::kConfirmQuit) {
            impl_->confirm_return = game.state().phase == Phase::kPlaying && game.state().paused == false
                                        ? Impl::Page::kPause
                                        : impl_->page;
        }
        impl_->capture_key.reset();
        impl_->capture_button.reset();
        impl_->page = Impl::Page::kConfirmQuit;
        impl_->selected = 1;
        return true;
    }
    void GameUi::ResetMenu()
    {
        ReleasePointer();
        impl_->hits.clear();
        impl_->previous.clear();
        impl_->page = Impl::Page::kTitle;
        impl_->selected = 0;
        impl_->menu_count = 0;
        impl_->capture_key.reset();
        impl_->capture_button.reset();
        impl_->binding_invalid = false;
        impl_->choice_request = 0;
        impl_->choice_selected = 0;
    }

    UiCommand GameUi::AdvancedKey(Game& game, std::uint32_t key)
    {
        if (game.state().phase == Phase::kFaulted) {
            return key == VK_ESCAPE ? UiCommand::kClose : UiCommand::kUnhandled;
        }
        using Page = Impl::Page;
        auto& state = *impl_;
        const auto page = state.page;
        const auto go = [&](Page next, std::uint32_t selected = 0) {
            state.page = next;
            state.selected = selected;
            return UiCommand::kHandled;
        };
        // 割当て中は通常のショートカットを通さず、予約キーも理由付きで拒否する。
        if (CapturingInput()) {
            if (key == VK_ESCAPE || (state.capture_button && key == VK_LEFT)) {
                state.capture_key.reset();
                state.capture_button.reset();
                state.binding_invalid = false;
            }
            else if (state.capture_key && state.input != nullptr) {
                state.binding_invalid =
                    state.input->BindKey(static_cast<InputAction>(*state.capture_key), key) == false;
                if (state.binding_invalid == false) {
                    state.capture_key.reset();
                }
            }
            return UiCommand::kHandled;
        }
        if (key == VK_F1 || key == VK_F2) {
            return UiCommand::kUnhandled;
        }
        const auto navigate = [&](std::uint32_t count, std::uint32_t& selected) {
            selected %= count;
            if (key == VK_UP || key == 'W' || key == VK_DOWN || key == 'S') {
                selected = (selected + (key == VK_UP || key == 'W' ? count - 1 : 1)) % count;
                return true;
            }
            return false;
        };
        if (page == Page::kConfirmQuit || page == Page::kConfirmAbandon || page == Page::kConfirmReset) {
            if (navigate(2, state.selected)) {
                return UiCommand::kHandled;
            }
            if (key == VK_ESCAPE || (key == VK_RETURN && state.selected == 1)) {
                return go(state.confirm_return);
            }
            if (key == VK_RETURN) {
                if (page == Page::kConfirmReset) {
                    if (state.reset_permitted) {
                        game.ResetProfile();
                    }
                    return go(Page::kRecords);
                }
                return page == Page::kConfirmQuit ? UiCommand::kClose : UiCommand::kAbandon;
            }
            return UiCommand::kHandled;
        }
        const auto playing = game.state().phase == Phase::kPlaying;
        const auto pause_key = state.input != nullptr && key == state.input->settings().keys[5];
        const auto direction_key = key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT || key == 'W' ||
                                   key == 'A' || key == 'S' || key == 'D';
        if (playing && game.state().paused == false) {
            const auto choosing = game.interaction().request != 0;
            const auto choice_key = direction_key || (key >= '1' && key <= '3');
            if (key == VK_ESCAPE || (pause_key && (choosing == false || choice_key == false))) {
                return UiCommand::kPause;
            }
            const auto& interaction = game.interaction();
            if (interaction.request != 0) {
                if (state.choice_request != interaction.request) {
                    state.choice_request = interaction.request;
                    state.choice_selected = 0;
                }
                if (navigate(interaction.count, state.choice_selected)) {
                    return UiCommand::kHandled;
                }
                if (key >= '1' && key < '1' + interaction.count) {
                    state.choice_selected = key - '1';
                    key = VK_RETURN;
                }
                if (key == VK_RETURN && game.Choose(interaction.request, state.choice_selected)) {
                    return UiCommand::kChosen;
                }
                return UiCommand::kHandled;
            }
            return UiCommand::kUnhandled;
        }
        if (game.state().phase == Phase::kResult) {
            if (navigate(3, state.selected)) {
                return UiCommand::kHandled;
            }
            if (key == VK_ESCAPE || (key == VK_RETURN && state.selected == 1)) {
                return UiCommand::kAbandon;
            }
            if (key == VK_RETURN && state.selected == 0) {
                return UiCommand::kBegin;
            }
            if (key == VK_RETURN) {
                RequestClose(game);
            }
            return UiCommand::kHandled;
        }
        if (page == Page::kPause) {
            if (key == VK_ESCAPE || (pause_key && direction_key == false)) {
                return UiCommand::kResume;
            }
            if (navigate(5, state.selected)) {
                return UiCommand::kHandled;
            }
            if (key == VK_RETURN) {
                switch (state.selected) {
                case 0:
                    return UiCommand::kResume;
                case 1:
                    return go(Page::kSettings);
                case 2:
                    return go(Page::kGuide);
                case 3:
                    state.confirm_return = Page::kPause;
                    return go(Page::kConfirmAbandon, 1);
                default:
                    RequestClose(game);
                    break;
                }
            }
            return UiCommand::kHandled;
        }
        if (page == Page::kSettings || page == Page::kControls) {
            const auto count = page == Page::kSettings ? 5u : 3u;
            if (navigate(count, state.selected)) {
                return UiCommand::kHandled;
            }
            if (key == VK_ESCAPE || (key == VK_RETURN && state.selected == count - 1)) {
                return go(page == Page::kControls ? Page::kSettings : playing ? Page::kPause : Page::kTitle);
            }
            if (key == VK_RETURN ||
                (page == Page::kSettings && state.selected == 3 && (key == VK_LEFT || key == VK_RIGHT))) {
                if (page == Page::kControls) {
                    return go(state.selected == 0 ? Page::kKeyboard : Page::kGamepad);
                }
                if (state.selected == 3) {
                    SwitchLanguage();
                }
                else {
                    return go(std::array{Page::kDisplay, Page::kAudio, Page::kControls}[state.selected]);
                }
            }
            return UiCommand::kHandled;
        }
        if (page == Page::kKeyboard || page == Page::kGamepad) {
            const auto keyboard = page == Page::kKeyboard;
            const auto actions =
                keyboard ? (game.mode() == GameMode::k3d ? 8u : 6u) : (game.mode() == GameMode::k3d ? 4u : 2u);
            if (navigate(actions + 2, state.selected)) {
                return UiCommand::kHandled;
            }
            if (key == VK_ESCAPE || (key == VK_RETURN && state.selected == actions + 1)) {
                return go(Page::kControls, keyboard ? 0u : 1u);
            }
            if (key == VK_RETURN && state.input != nullptr) {
                if (state.selected == actions) {
                    auto settings = state.input->settings();
                    if (keyboard) {
                        settings.keys = InputSettings{}.keys;
                    }
                    else {
                        settings.buttons = InputSettings{}.buttons;
                    }
                    state.input->SetSettings(settings);
                }
                else {
                    (keyboard ? state.capture_key : state.capture_button) = state.selected;
                    state.binding_invalid = false;
                }
            }
            return UiCommand::kHandled;
        }
        if (playing && page == Page::kGuide && (key == VK_ESCAPE || key == VK_RETURN)) {
            return go(Page::kPause, 2);
        }
        if (page == Page::kTitle && (key == VK_ESCAPE || (key == VK_RETURN && state.selected == 6))) {
            RequestClose(game);
            return UiCommand::kHandled;
        }
        return UiCommand::kUnhandled;
    }

    // ページと選択を管理し、ゲーム開始・停止の実処理はPlayWindowへ返す。
    UiCommand GameUi::Key(Game& game, std::uint32_t key)
    {
        impl_->previous.clear();
        impl_->hits.clear();
        ReleasePointer();
        if (impl_->input_settings) {
            const auto command = AdvancedKey(game, key);
            if (command != UiCommand::kUnhandled) {
                return command;
            }
        }
        if (game.HasProfile() == false ||
            (game.state().phase != Phase::kStart && (impl_->input_settings == false || game.state().paused == false))) {
            return UiCommand::kUnhandled;
        }
        auto* display = impl_->display;
        const auto settings = impl_->page == Impl::Page::kDisplay;
        const auto audio_page = impl_->page == Impl::Page::kAudio;
        const auto pending = settings && display != nullptr && display->pending();
        const auto count = pending      ? 2u
                           : audio_page ? 6u
                           : settings   ? (impl_->audio_settings && impl_->input_settings == false ? 7u : 6u)
                           : impl_->page == Impl::Page::kTitle ? (impl_->display_settings ? 7u : 6u)
                           : impl_->page == Impl::Page::kWorkshop
                               ? static_cast<std::uint32_t>(game.ProfileActions().size()) + 1
                           : impl_->page == Impl::Page::kRecords && impl_->reset_permitted ? 2u
                                                                                           : 1u;
        impl_->selected %= count;
        // 音量は即時反映し、戻る操作でも保持する。表示設定の確認待ちとは分離する。
        if (audio_page) {
            if (key == VK_ESCAPE || (key == VK_RETURN && impl_->selected == 5)) {
                impl_->page = impl_->input_settings ? Impl::Page::kSettings : Impl::Page::kDisplay;
                impl_->selected = impl_->input_settings ? 1u : 5u;
                return UiCommand::kHandled;
            }
            if (impl_->audio != nullptr &&
                (key == VK_LEFT || key == 'A' || key == VK_RIGHT || key == 'D' || key == VK_RETURN)) {
                auto value = impl_->audio->settings();
                if (impl_->selected < 3) {
                    auto* volume = impl_->selected == 0   ? &value.master
                                   : impl_->selected == 1 ? &value.music
                                                          : &value.effects;
                    const auto direction = key == VK_LEFT || key == 'A' ? -10 : 10;
                    *volume = static_cast<std::uint32_t>(std::clamp(int(*volume) + direction, 0, 100));
                }
                else if (impl_->selected == 3) {
                    value.muted = value.muted == false;
                }
                impl_->audio->SetSettings(value);
                if (impl_->selected == 4) {
                    impl_->audio->TestEffect();
                }
                return UiCommand::kHandled;
            }
        }
        if (settings && pending == false && (key == VK_LEFT || key == 'A' || key == VK_RIGHT || key == 'D')) {
            if (display != nullptr && impl_->selected < 4) {
                display->Cycle(impl_->selected, key == VK_LEFT || key == 'A' ? -1 : 1);
            }
            return UiCommand::kHandled;
        }
        if (key == VK_UP || key == 'W' || key == VK_DOWN || key == 'S') {
            const auto direction = key == VK_UP || key == 'W' ? count - 1 : 1;
            impl_->selected = (impl_->selected + direction) % count;
            return UiCommand::kHandled;
        }
        if (key == VK_ESCAPE) {
            if (settings && display != nullptr) {
                display->Cancel();
            }
            if (pending) {
                impl_->selected = 4;
                return UiCommand::kHandled;
            }
            if (settings && impl_->input_settings) {
                impl_->page = Impl::Page::kSettings;
                impl_->selected = 0;
            }
            else {
                ResetMenu();
            }
            return UiCommand::kHandled;
        }
        if (key != VK_RETURN) {
            return UiCommand::kUnhandled;
        }
        if (settings) {
            if (display != nullptr && pending) {
                if (impl_->selected == 0) {
                    display->Keep();
                }
                else {
                    display->Cancel();
                }
                impl_->selected = 4;
            }
            else if (impl_->selected < 4 && display != nullptr) {
                display->Cycle(impl_->selected, 1);
            }
            else if (impl_->selected == 4 && display != nullptr) {
                display->Apply();
                impl_->selected = 0;
            }
            else if (impl_->selected == 5) {
                if (impl_->audio_settings && impl_->input_settings == false) {
                    impl_->page = Impl::Page::kAudio;
                    impl_->selected = 0;
                    return UiCommand::kHandled;
                }
                if (display != nullptr) {
                    display->Cancel();
                }
                if (impl_->input_settings) {
                    impl_->page = Impl::Page::kSettings;
                    impl_->selected = 0;
                }
                else {
                    ResetMenu();
                }
            }
            else if (impl_->selected == 6) {
                if (display != nullptr) {
                    display->Cancel();
                }
                ResetMenu();
            }
            return UiCommand::kHandled;
        }
        if (impl_->page == Impl::Page::kTitle) {
            if (impl_->selected == 0) {
                return UiCommand::kBegin;
            }
            if (impl_->selected == count - 1) {
                return UiCommand::kClose;
            }
            const auto pages = std::array{Impl::Page::kTitle,   Impl::Page::kWorkshop, Impl::Page::kRecords,
                                          Impl::Page::kDisplay, Impl::Page::kGuide,    Impl::Page::kCredits};
            const auto index = impl_->selected + (impl_->display_settings == false && impl_->selected >= 3 ? 1 : 0);
            impl_->page = pages[index];
            if (impl_->page == Impl::Page::kDisplay && impl_->input_settings) {
                impl_->page = Impl::Page::kSettings;
            }
            impl_->selected = 0;
        }
        else if (impl_->page == Impl::Page::kWorkshop && impl_->selected + 1 < count) {
            game.ActOnProfile(impl_->selected);
        }
        else if (impl_->page == Impl::Page::kRecords && impl_->reset_permitted && impl_->selected == 0) {
            impl_->confirm_return = Impl::Page::kRecords;
            impl_->page = Impl::Page::kConfirmReset;
            impl_->selected = 1;
        }
        else {
            ResetMenu();
        }
        return UiCommand::kHandled;
    }

    // 直前に描いたメニュー行を同じ決定処理へ送り、表示と操作の位置を揃える。
    UiCommand GameUi::Click(Game& game, float x, float y)
    {
        if (impl_->input_settings) {
            // 操作部だけを当たり判定にする。項目名や余白のクリックでは値を変更しない。
            for (const auto& hit : impl_->hits) {
                if (hit.Contains(x, y)) {
                    if (game.interaction().request != 0 && game.state().paused == false) {
                        impl_->choice_selected = hit.row;
                    }
                    else {
                        impl_->selected = hit.row;
                    }
                    if (hit.slider) {
                        impl_->drag = hit;
                        return PointerMove(game, x, y);
                    }
                    return Key(game, hit.key);
                }
            }
            return UiCommand::kUnhandled;
        }
        if (game.HasProfile() == false || x < impl_->menu_left || x > impl_->menu_right ||
            (game.state().phase != Phase::kStart &&
             (impl_->input_settings == false || (game.state().phase != Phase::kResult && game.state().paused == false &&
                                                 game.interaction().request == 0)))) {
            return UiCommand::kUnhandled;
        }
        const auto row = (y - impl_->menu_top) / impl_->row_step - float(impl_->first_line);
        if (row < 0 || row >= float(impl_->menu_count)) {
            return UiCommand::kUnhandled;
        }
        if (impl_->input_settings && game.interaction().request != 0 && game.state().paused == false) {
            impl_->choice_selected = static_cast<std::uint32_t>(row);
        }
        else {
            impl_->selected = static_cast<std::uint32_t>(row);
        }
        return Key(game, VK_RETURN);
    }

    UiCommand GameUi::PointerMove(Game& game, float x, float y)
    {
        if (impl_->drag && impl_->audio != nullptr && impl_->page == Impl::Page::kAudio) {
            const auto& hit = *impl_->drag;
            auto settings = impl_->audio->settings();
            auto* volume = hit.row == 0 ? &settings.master : hit.row == 1 ? &settings.music : &settings.effects;
            *volume = static_cast<std::uint32_t>(std::clamp((x - hit.x) / hit.width * 100.f + .5f, 0.f, 100.f));
            impl_->audio->SetSettings(settings);
            return UiCommand::kHandled;
        }
        if (impl_->input_settings && CapturingInput() == false) {
            for (const auto& hit : impl_->hits) {
                if (hit.Contains(x, y)) {
                    if (game.interaction().request != 0 && game.state().paused == false) {
                        impl_->choice_selected = hit.row;
                    }
                    else {
                        impl_->selected = hit.row;
                    }
                    break;
                }
            }
        }
        return UiCommand::kUnhandled;
    }

    void GameUi::ReleasePointer()
    {
        impl_->drag.reset();
    }

    bool GameUi::PointerActive() const
    {
        return impl_->drag.has_value();
    }

    rengine::Draw GameUi::Draw(Game& game, SaveStatus save_status)
    {
        const auto& state = game.state();
        const auto& interaction = game.interaction();
        const auto& words = impl_->messages[impl_->language];
        auto text = std::wstring{};
        impl_->menu_count = 0;
        Require(impl_->profile_reset == false || game.HasProfileReset(), "profile reset export is missing");
        impl_->reset_permitted =
            impl_->profile_reset && game.HasProfileReset() && save_status != SaveStatus::kProtected;
        const auto modal = state.phase != Phase::kPlaying || state.paused || interaction.request != 0;
        if (state.phase == Phase::kFaulted) {
            text = L"Runtime error - Escape";
        }
        else if (state.phase == Phase::kStart ||
                 (impl_->input_settings &&
                  ((state.phase == Phase::kPlaying && state.paused) || impl_->page == Impl::Page::kConfirmQuit))) {
            if (game.HasProfile() == false) {
                text = words.at("title") + L"\n\n" + words.at("start") + L"\n" + words.at("controls");
            }
            else {
                Require(impl_->profile_id.empty() == false, "profile requires UI metadata");
                const auto value = [&](std::uint32_t slot) {
                    Require(slot < game.profile().count, "profile UI slot is missing");
                    return std::to_wstring(game.profile().values[slot]);
                };
                const auto balance = words.at("balance") + L" " + value(impl_->balance_slot);
                auto items = std::vector<std::wstring>{};
                if (impl_->page == Impl::Page::kConfirmQuit || impl_->page == Impl::Page::kConfirmAbandon ||
                    impl_->page == Impl::Page::kConfirmReset) {
                    text = words.at(impl_->page == Impl::Page::kConfirmQuit    ? "confirm_quit"
                                    : impl_->page == Impl::Page::kConfirmReset ? "confirm_reset"
                                                                               : "confirm_abandon") +
                           L"\n\n";
                    items = {words.at("yes"), words.at("no")};
                }
                else if (impl_->page == Impl::Page::kPause) {
                    text = words.at("paused") + L"\n\n";
                    items = {words.at("resume"), words.at("settings"), words.at("guide"), words.at("return_title"),
                             words.at("quit")};
                }
                else if (impl_->page == Impl::Page::kSettings) {
                    text = words.at("settings") + L"\n";
                    items = {words.at("display"), words.at("audio"), words.at("controls_settings"),
                             words.at("language") + (impl_->language == 0 ? L" : 日本語" : L" : English"),
                             words.at("return")};
                }
                else if (impl_->page == Impl::Page::kControls) {
                    text = words.at("controls_settings") + L"\n";
                    items = {words.at("keyboard"), words.at("controller"), words.at("return")};
                }
                else if (impl_->page == Impl::Page::kKeyboard || impl_->page == Impl::Page::kGamepad) {
                    const auto keyboard = impl_->page == Impl::Page::kKeyboard;
                    text = words.at(keyboard ? "keyboard" : "controller") + L"\n";
                    if (keyboard == false && (impl_->input == nullptr || impl_->input->pad_connected() == false)) {
                        text += words.at("pad_unavailable") + L"\n";
                    }
                    const auto settings = impl_->input ? impl_->input->settings() : InputSettings{};
                    const auto labels = std::array{"move_left",    "move_right",   "move_up",        "move_down",
                                                   "focus_action", "pause_action", "descend_action", "ascend_action"};
                    const auto actions = game.mode() == GameMode::k3d ? 8u : 6u;
                    for (auto index = keyboard ? 0u : 4u; index < actions; ++index) {
                        items.push_back(words.at(labels[index]) + L" : " +
                                        (keyboard ? InputKeyName(settings.keys[index])
                                                  : InputButtonName(settings.buttons[index - 4])));
                    }
                    items.push_back(words.at("defaults"));
                    items.push_back(words.at("return"));
                }
                else if (impl_->page == Impl::Page::kTitle) {
                    text = TitleMenuVisible(game) ? L"" : words.at("title") + L"\n";
                    for (const auto* name : {"play", "workshop", "records", "settings", "guide", "credits", "quit"}) {
                        if (std::string_view(name) != "settings" || impl_->display_settings) {
                            items.push_back(words.at(name));
                        }
                    }
                }
                else if (impl_->page == Impl::Page::kWorkshop) {
                    text = words.at("workshop") + L"   " + balance + L"\n";
                    impl_->workshop_actions = game.ProfileActions();
                    for (const auto& action : impl_->workshop_actions) {
                        Require(action.label < impl_->upgrades[impl_->language].size(), "profile action label missing");
                        auto label = impl_->upgrades[impl_->language][action.label] + L"  " +
                                     std::to_wstring(action.rank) + L"/" + std::to_wstring(action.maximum);
                        if (action.rank == action.maximum) {
                            label += L"  " + words.at("capped");
                        }
                        else {
                            label += L"  " + words.at("cost") + L" " + std::to_wstring(action.cost);
                            if (action.enabled == false) {
                                label += L"  " + words.at("locked");
                            }
                        }
                        items.push_back(label);
                    }
                    items.push_back(words.at("return"));
                }
                else if (impl_->page == Impl::Page::kDisplay) {
                    const auto* display = impl_->display;
                    const auto available = display != nullptr && display->enabled();
                    const auto pending = available && display->pending();
                    text = words.at("settings") + L"\n";
                    if (pending) {
                        text += words.at("confirm_display") + L" " + std::to_wstring(display->seconds()) + L"\n";
                        items = {words.at("keep"), words.at("cancel")};
                    }
                    else {
                        const auto settings_value = display != nullptr ? display->draft() : DisplaySettings{};
                        const auto mode = settings_value.mode == DisplayMode::kWindowed     ? "windowed"
                                          : settings_value.mode == DisplayMode::kBorderless ? "borderless"
                                                                                            : "fullscreen";
                        auto monitor = std::uint32_t{1};
                        if (display != nullptr) {
                            for (auto index = 0u; index < display->monitors().size(); ++index) {
                                if (display->monitors()[index].id == settings_value.monitor) {
                                    monitor = index + 1;
                                }
                            }
                        }
                        items = {words.at("display_mode") + L" : " + words.at(mode),
                                 words.at("resolution") + L" : " +
                                     (settings_value.mode == DisplayMode::kBorderless
                                          ? words.at("desktop_resolution")
                                          : std::to_wstring(settings_value.resolution.width) + L" x " +
                                                std::to_wstring(settings_value.resolution.height)),
                                 words.at("monitor") + L" : " + std::to_wstring(monitor),
                                 words.at("vsync") + L" : " + words.at(settings_value.vsync ? "on" : "off"),
                                 words.at("apply"),
                                 words.at("return")};
                        if (impl_->audio_settings && impl_->input_settings == false) {
                            items.insert(items.end() - 1, words.at("audio"));
                        }
                    }
                    if (available == false) {
                        text += words.at("display_unavailable") + L"\n";
                    }
                    else if (display->failed()) {
                        text += words.at("display_failed") + L"\n";
                    }
                    impl_->selected %= static_cast<std::uint32_t>(items.size());
                }
                else if (impl_->page == Impl::Page::kAudio) {
                    text = words.at("audio") + L"\n";
                    const auto audio = impl_->audio;
                    const auto settings = audio ? audio->settings() : AudioSettings{};
                    if (audio == nullptr || audio->available() == false) {
                        text += words.at("audio_unavailable") + L"\n";
                    }
                    items = {words.at("master") + L" : " + std::to_wstring(settings.master) + L"%",
                             words.at("music") + L" : " + std::to_wstring(settings.music) + L"%",
                             words.at("effects") + L" : " + std::to_wstring(settings.effects) + L"%",
                             words.at("muted") + L" : " + words.at(settings.muted ? "on" : "off"),
                             words.at("test_audio"),
                             words.at("return")};
                }
                else if (impl_->page == Impl::Page::kRecords) {
                    text = words.at("records") + L"   " + balance + L"\n";
                    for (auto index = std::size_t{0}; index < impl_->record_slots.size(); ++index) {
                        text +=
                            impl_->records[impl_->language][index] + L" " + value(impl_->record_slots[index]) + L"\n";
                    }
                    if (impl_->reset_permitted) {
                        items.push_back(words.at("reset_progress"));
                    }
                    items.push_back(words.at("return"));
                }
                else {
                    const auto guide = impl_->page == Impl::Page::kGuide;
                    text = words.at(guide ? "guide" : "credits") + L"\n" +
                           words.at(guide ? "guide_text" : "credits_text") + L"\n";
                    items.push_back(words.at("return"));
                }
                impl_->menu_count = static_cast<std::uint32_t>(items.size());
                impl_->first_line = static_cast<std::uint32_t>(std::count(text.begin(), text.end(), L'\n'));
                for (auto index = 0u; index < items.size(); ++index) {
                    text += (index == impl_->selected ? L"> " : L"  ") + items[index] + L"\n";
                }
                const auto editing_settings = impl_->page == Impl::Page::kAudio ||
                                              (impl_->page == Impl::Page::kDisplay &&
                                               (impl_->display == nullptr || impl_->display->pending() == false));
                text +=
                    words.at(impl_->input_settings && impl_->input && impl_->input->using_pad() ? "menu_navigation_pad"
                             : editing_settings                                                 ? "settings_navigation"
                                                                                                : "navigation");
            }
        }
        else if (state.phase == Phase::kResult) {
            text = words.at(state.outcome == 1 ? "clear" : "fail") + L"\n\n";
            if (impl_->result_details) {
                // 結果の数値は終了時の通常状態を使い、別の計測時計やゲーム固有exportを作らない。
                const auto seconds = state.tick / 60;
                text += words.at("elapsed") + L" " + std::to_wstring(seconds / 60) + L":" +
                        (seconds % 60 < 10 ? L"0" : L"") + std::to_wstring(seconds % 60) + L"   " +
                        words.at("remaining_hp") + L" " + std::to_wstring(std::max(0, state.hp)) + L"\n";
                for (const auto slot : impl_->result_status) {
                    Require(slot < state.status_count, "result status slot is missing");
                    const auto& value = state.status[slot];
                    Require(value.label < impl_->status[impl_->language].size(), "result status label is missing");
                    text += impl_->status[impl_->language][value.label] + L" " + std::to_wstring(value.value) + L"   ";
                }
                text += L"\n";
            }
            if (game.HasProfile()) {
                Require(impl_->reward_slot < game.profile().count, "profile reward slot missing");
                text += words.at("reward") + L" " + std::to_wstring(game.profile().values[impl_->reward_slot]) + L"\n";
            }
            if (impl_->input_settings) {
                impl_->menu_count = 3;
                impl_->first_line = static_cast<std::uint32_t>(std::count(text.begin(), text.end(), L'\n'));
                const auto labels = std::array{"retry", "return_title", "quit"};
                impl_->selected %= 3;
                for (auto index = 0u; index < labels.size(); ++index) {
                    text += (index == impl_->selected ? L"> " : L"  ") + words.at(labels[index]) + L"\n";
                }
                text += words.at(impl_->input && impl_->input->using_pad() ? "menu_navigation_pad" : "navigation");
            }
            else {
                text += words.at("back");
            }
        }
        else if (state.paused) {
            text = words.at("paused") + L"\n\n" + words.at("paused_controls");
        }
        else if (interaction.request != 0) {
            text = words.at("choose") + L"\n\n";
            if (impl_->choice_request != interaction.request) {
                impl_->choice_request = interaction.request;
                impl_->choice_selected = 0;
            }
            impl_->first_line = 2;
            impl_->menu_count = interaction.count;
            for (auto index = 0u; index < interaction.count; ++index) {
                const auto id = interaction.options[index];
                Require(id < impl_->options[impl_->language].size(), "UI option ID is not defined");
                text += (impl_->input_settings && index == impl_->choice_selected ? L"> " : L"  ") +
                        std::to_wstring(index + 1) + L"  " + impl_->options[impl_->language][id] + L"\n";
            }
            if (impl_->input_settings) {
                text +=
                    words.at(impl_->input && impl_->input->using_pad() ? "choice_navigation_pad" : "choice_navigation");
            }
        }
        else {
            text = L"HP " + std::to_wstring(state.hp);
            if (state.status_count == 0) {
                text = words.at("controls") + L"   " + text + L"   / " + std::to_wstring(state.enemy_hp);
            }
            for (auto index = 0u; index < state.status_count; ++index) {
                const auto& value = state.status[index];
                Require(value.label < impl_->status[impl_->language].size(), "UI status ID is not defined");
                text += L"    " + impl_->status[impl_->language][value.label] + L" " + std::to_wstring(value.value);
                if (value.maximum > 0) {
                    text += L"/" + std::to_wstring(value.maximum);
                }
            }
        }
        auto notice = std::wstring{};
        if (modal && game.HasProfile()) {
            const auto key = save_status == SaveStatus::kMemory      ? "preview"
                             : save_status == SaveStatus::kRecovered ? "recovered"
                             : save_status == SaveStatus::kProtected ? "protected"
                             : save_status == SaveStatus::kFailed    ? "save_failed"
                                                                     : nullptr;
            if (key != nullptr) {
                notice = words.at(key);
                text += L"\n" + words.at(key);
            }
        }
        if (modal) {
            text += L"\nF1 : 日本語 / English";
        }
        if (impl_->input_settings) {
            impl_->MenuLayout(text, modal, TitleMenuVisible(game), game, notice);
        }
        else {
            impl_->Layout(text, modal, TitleMenuVisible(game));
        }
        auto result = rengine::Draw{};
        result.kind = 1;
        result.material = impl_->material;
        result.instances = impl_->instances.data();
        result.instance_count = static_cast<std::uint32_t>(impl_->instances.size());
        return result;
    }
} // namespace gengine
