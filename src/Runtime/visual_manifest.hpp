#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gengine
{
    // 一枚のatlasを個体・弾で共用し、画像の切出しはUVだけで指定する。
    // 個体のvisual 2以降をspritesの0始まりの要素へ対応させる。
    struct VisualManifest
    {
        std::string backdrop;
        std::string title_logo;
        std::string atlas;
        std::vector<std::array<float, 4>> sprites;
        std::string detail_atlas;
        std::vector<std::array<float, 4>> detail_sprites;
        std::string environment_atlas;
        std::vector<std::array<float, 4>> environments;
        std::uint32_t hostile = 0;
        std::uint32_t friendly = 0;
        std::array<float, 3> hostile_color{1.f, .38f, .3f};
        std::array<float, 3> friendly_color{.4f, 1.f, .9f};
        float combat_brightness = .12f;
    };

    // RuntimeとEditorのビルドで上限・UV・参照形式を同じ条件に揃える。
    VisualManifest DecodeVisualManifest(std::span<const std::uint8_t> bytes);
    std::string SerializeVisualManifest(const VisualManifest& manifest);
} // namespace gengine
