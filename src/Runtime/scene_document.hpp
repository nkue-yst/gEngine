#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gengine
{
    struct SceneSettings
    {
        std::vector<float> bounds_min;
        std::vector<float> bounds_max;
        std::int32_t player_hp = 3;
        std::int32_t enemy_hp = 30;
        float move_speed = 5;
        float fire_interval_seconds = 0.1f;
        bool operator==(const SceneSettings&) const = default;
    };

    struct SceneObject
    {
        std::string id;
        std::string kind;
        std::vector<float> position;
        std::vector<float> target;
        std::vector<float> view_size;
        float fire_interval_seconds = 0.5f;
        float fov_degrees = 60;
        std::optional<std::string> visual;
        std::optional<std::string> audio;
        bool operator==(const SceneObject&) const = default;
    };

    // 編集時の初期条件だけを所有する。VMの現在位置や弾、履歴は保存しない。
    struct SceneDocument
    {
        std::uint32_t format_version = 1;
        std::string document_id;
        std::string game;
        SceneSettings settings;
        std::vector<SceneObject> objects;
        bool operator==(const SceneDocument&) const = default;
    };

    SceneDocument LoadProjectScene(const std::filesystem::path& project, std::string_view scene_path);
    SceneDocument DefaultScene(bool three_dimensional);
    SceneObject DefaultSceneObject(std::string_view game, std::string_view kind, std::string id);
    // 不正な形式・値は理由付きの例外。読込み失敗は呼出し側の文書を変更しない。
    SceneDocument ParseScene(std::string_view text);
    std::string SerializeScene(const SceneDocument& scene);
    void ValidateScene(const SceneDocument& scene);
    void ValidateSceneForPlay(const SceneDocument& scene);
    // 素材はプロジェクト内の通常ファイルだけを参照する。リンク経由の外部参照も拒否する。
    void ValidateSceneReferences(const SceneDocument& scene, const std::filesystem::path& project_root);
} // namespace gengine
