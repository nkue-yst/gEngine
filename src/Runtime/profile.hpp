#pragma once
#include "audio_settings.hpp"
#include "display_settings.hpp"
#include "input_settings.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace gengine
{
    // ゲーム側が意味と移行を管理する固定長の値。ホストは保存と版の伝達を担当する。
    struct ProfileData
    {
        std::uint32_t schema = 0;
        std::uint32_t count = 0;
        std::array<std::uint32_t, 32> values{};
        bool operator==(const ProfileData&) const = default;
    };
    struct ProfileAction
    {
        std::uint32_t label = 0;
        std::uint32_t rank = 0;
        std::uint32_t maximum = 0;
        std::uint32_t cost = 0;
        bool enabled = false;
    };
    struct SavedProfile
    {
        ProfileData game;
        std::uint32_t language = 0;
        DisplaySettings display;
        AudioSettings audio;
        InputSettings input;
        bool operator==(const SavedProfile&) const = default;
    };
    enum class SaveStatus
    {
        kMemory,
        kReady,
        kRecovered,
        kProtected,
        kFailed
    };
    // 空の指定はメモリーのみ。Runnerだけが通常の利用者保存を明示的に有効にする。
    struct ProfileStorageOptions
    {
        bool persistent = false;
        std::filesystem::path directory;
    };
    class ProfileStore final
    {
      public:
        ProfileStore(std::string game_id, ProfileStorageOptions options);
        ~ProfileStore();
        // ゲームによる値の検証も復旧候補ごとに行う。未来のschemaは巻き戻さず保護する。
        std::optional<SavedProfile> Load(std::uint32_t maximum_schema = UINT32_MAX,
                                         const std::function<bool(const ProfileData&)>& validate = {});
        bool Save(const SavedProfile& value);
        // ゲームの検証で拒否された記録を上書きしない。
        void Protect();
        SaveStatus status() const;
        const std::filesystem::path& directory() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace gengine
