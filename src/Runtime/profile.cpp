#include "profile.hpp"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <shlobj.h>
#include <windows.h>

namespace gengine
{
    namespace
    {
        constexpr auto kMaxSaveBytes = std::uintmax_t{65536};

        std::uint32_t Unsigned(const nlohmann::json& value)
        {
            if (value.is_number_unsigned() == false || value.get<std::uint64_t>() > UINT32_MAX) {
                throw std::invalid_argument("profile value requires u32");
            }
            return value.get<std::uint32_t>();
        }

        std::string Encode(const SavedProfile& value, const std::string& id)
        {
            if (value.game.schema == 0 || value.game.count == 0 || value.game.count > 32 || value.language > 1 ||
                ValidDisplaySettings(value.display) == false || ValidAudioSettings(value.audio) == false ||
                ValidInputSettings(value.input) == false) {
                throw std::invalid_argument("invalid saved profile");
            }
            const auto values =
                std::vector<std::uint32_t>(value.game.values.begin(), value.game.values.begin() + value.game.count);
            return nlohmann::json{{"format", 4},
                                  {"game", id},
                                  {"schema", value.game.schema},
                                  {"values", values},
                                  {"language", value.language},
                                  {"display",
                                   {{"mode", static_cast<std::uint32_t>(value.display.mode)},
                                    {"width", value.display.resolution.width},
                                    {"height", value.display.resolution.height},
                                    {"vsync", value.display.vsync},
                                    {"monitor", value.display.monitor}}},
                                  {"audio",
                                   {{"master", value.audio.master},
                                    {"music", value.audio.music},
                                    {"effects", value.audio.effects},
                                    {"muted", value.audio.muted}}},
                                  {"input", {{"keys", value.input.keys}, {"buttons", value.input.buttons}}}}
                       .dump(2) +
                   "\n";
        }

        // 同じディレクトリー内で書込みを完了してから交換する。tmpは復旧候補にしない。
        void WriteFileAtomically(const std::filesystem::path& path, const std::string& bytes)
        {
            auto temporary = path;
            temporary += L".tmp";
            const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                throw std::runtime_error("cannot open temporary profile");
            }
            auto written = DWORD{0};
            const auto success =
                WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
                written == bytes.size() && FlushFileBuffers(handle) != FALSE;
            CloseHandle(handle);
            if (success == false || MoveFileExW(temporary.c_str(), path.c_str(),
                                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
                throw std::runtime_error("cannot commit profile");
            }
        }
    } // namespace

    struct ProfileStore::Impl
    {
        std::string id;
        std::filesystem::path directory;
        HANDLE lock = INVALID_HANDLE_VALUE;
        SaveStatus status = SaveStatus::kMemory;
        bool persistent = false;
        bool protected_data = false;
        bool loaded = false;
        std::optional<SavedProfile> previous;

        ~Impl()
        {
            if (lock != INVALID_HANDLE_VALUE) {
                CloseHandle(lock);
            }
        }

        std::optional<SavedProfile> Read(const std::filesystem::path& path, std::uint32_t maximum_schema,
                                         const std::function<bool(const ProfileData&)>& validate)
        {
            if (std::filesystem::exists(path) == false) {
                return {};
            }
            if (std::filesystem::file_size(path) > kMaxSaveBytes) {
                throw std::invalid_argument("profile exceeds size limit");
            }
            auto file = std::ifstream(path, std::ios::binary);
            const auto data = nlohmann::json::parse(file);
            // 新しい形式を古い記録へ巻き戻さず、利用中のファイルを保護する。
            if (Unsigned(data.at("format")) > 4 || data.at("game") != id) {
                protected_data = true;
            }
            if (Unsigned(data.at("format")) < 1 || Unsigned(data.at("format")) > 4 || data.at("game") != id) {
                throw std::invalid_argument("unsupported profile format or game");
            }
            const auto& values = data.at("values");
            if (values.is_array() == false || values.empty() || values.size() > 32) {
                throw std::invalid_argument("invalid profile size");
            }
            auto result = SavedProfile{};
            result.game.schema = Unsigned(data.at("schema"));
            result.game.count = static_cast<std::uint32_t>(values.size());
            result.language = Unsigned(data.at("language"));
            // 形式1の利用者記録は維持し、追加した設定だけをFHDの既定値にする。
            if (Unsigned(data.at("format")) >= 2) {
                const auto& display = data.at("display");
                result.display.mode = static_cast<DisplayMode>(Unsigned(display.at("mode")));
                result.display.resolution = {Unsigned(display.at("width")), Unsigned(display.at("height"))};
                result.display.vsync = display.at("vsync").get<bool>();
                result.display.monitor = display.at("monitor").get<std::string>();
                if (ValidDisplaySettings(result.display) == false) {
                    throw std::invalid_argument("invalid saved display settings");
                }
            }
            // 形式1・2は既存の進行と表示設定を保持し、音量だけを既定値で補う。
            if (Unsigned(data.at("format")) >= 3) {
                const auto& audio = data.at("audio");
                result.audio = {Unsigned(audio.at("master")), Unsigned(audio.at("music")),
                                Unsigned(audio.at("effects")), audio.at("muted").get<bool>()};
                if (ValidAudioSettings(result.audio) == false) {
                    throw std::invalid_argument("invalid saved audio settings");
                }
            }
            if (result.game.schema == 0 || result.language > 1) {
                throw std::invalid_argument("invalid profile metadata");
            }
            if (Unsigned(data.at("format")) >= 4) {
                const auto read_bindings = [&](const char* name, auto& bindings) {
                    const auto& entries = data.at("input").at(name);
                    if (entries.is_array() == false || entries.size() != bindings.size()) {
                        throw std::invalid_argument("invalid input binding count");
                    }
                    for (auto index = 0u; index < bindings.size(); ++index) {
                        bindings[index] = Unsigned(entries[index]);
                    }
                };
                read_bindings("keys", result.input.keys);
                read_bindings("buttons", result.input.buttons);
                if (ValidInputSettings(result.input) == false) {
                    throw std::invalid_argument("invalid saved input bindings");
                }
            }
            if (result.game.schema > maximum_schema) {
                protected_data = true;
                throw std::invalid_argument("profile schema is newer than this game");
            }
            for (auto index = 0u; index < result.game.count; ++index) {
                result.game.values[index] = Unsigned(values[index]);
            }
            if (validate && validate(result.game) == false) {
                throw std::invalid_argument("game rejected saved profile");
            }
            return result;
        }
    };

    ProfileStore::ProfileStore(std::string game_id, ProfileStorageOptions options) : impl_(std::make_unique<Impl>())
    {
        if (game_id.empty() || game_id.size() > 64 || game_id.front() < 'a' || game_id.front() > 'z' ||
            std::any_of(game_id.begin(), game_id.end(), [](char c) {
                return (c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '-';
            })) {
            throw std::invalid_argument("profile game id requires lowercase letters, digits and hyphens");
        }
        impl_->id = std::move(game_id);
        impl_->persistent = options.persistent;
        if (options.persistent == false) {
            return;
        }
        try {
            if (options.directory.empty()) {
                auto* local = static_cast<wchar_t*>(nullptr);
                if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
                    throw std::runtime_error("local application data is unavailable");
                }
                options.directory = std::filesystem::path(local) / "gEngine/Games" / impl_->id;
                CoTaskMemFree(local);
            }
            impl_->directory = std::filesystem::absolute(options.directory).lexically_normal();
            std::filesystem::create_directories(impl_->directory);
            // 別プロセスの起動中は読み込みも書込みもしない。終了時にOSがロックを解放する。
            impl_->lock = CreateFileW((impl_->directory / "profile.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (impl_->lock == INVALID_HANDLE_VALUE) {
                throw std::runtime_error("profile is in use or unavailable");
            }
            impl_->status = SaveStatus::kReady;
        } catch (const std::exception&) {
            Protect();
        }
    }
    ProfileStore::~ProfileStore() = default;

    std::optional<SavedProfile> ProfileStore::Load(std::uint32_t maximum_schema,
                                                   const std::function<bool(const ProfileData&)>& validate)
    {
        if (impl_->loaded || impl_->persistent == false || impl_->protected_data) {
            return impl_->previous;
        }
        impl_->loaded = true;
        try {
            impl_->previous = impl_->Read(impl_->directory / "profile.json", maximum_schema, validate);
            if (impl_->previous) {
                return impl_->previous;
            }
            if (std::filesystem::exists(impl_->directory / "profile.backup.json") == false) {
                return {};
            }
        } catch (const std::exception&) {
            if (impl_->protected_data) {
                Protect();
                return {};
            }
        }
        try {
            impl_->previous = impl_->Read(impl_->directory / "profile.backup.json", maximum_schema, validate);
            if (impl_->previous) {
                impl_->status = SaveStatus::kRecovered;
                return impl_->previous;
            }
        } catch (const std::exception&) {
        }
        Protect();
        return {};
    }

    bool ProfileStore::Save(const SavedProfile& value)
    {
        if (impl_->protected_data || (impl_->persistent && impl_->loaded == false)) {
            return false;
        }
        try {
            const auto bytes = Encode(value, impl_->id);
            if (impl_->persistent == false) {
                impl_->previous = value;
                return true;
            }
            if (impl_->previous == value && impl_->status == SaveStatus::kReady) {
                return true;
            }
            // 復旧時も、壊れた本体ではなく検証済みの値をバックアップにする。
            WriteFileAtomically(impl_->directory / "profile.backup.json",
                                Encode(impl_->previous.value_or(value), impl_->id));
            WriteFileAtomically(impl_->directory / "profile.json", bytes);
            impl_->previous = value;
            impl_->status = SaveStatus::kReady;
            return true;
        } catch (const std::exception&) {
            impl_->status = SaveStatus::kFailed;
            return false;
        }
    }

    void ProfileStore::Protect()
    {
        impl_->protected_data = true;
        impl_->status = SaveStatus::kProtected;
    }
    SaveStatus ProfileStore::status() const
    {
        return impl_->status;
    }
    const std::filesystem::path& ProfileStore::directory() const
    {
        return impl_->directory;
    }
} // namespace gengine
