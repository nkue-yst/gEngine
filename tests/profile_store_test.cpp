#include "profile.hpp"
#include <fstream>
#include <iostream>
#include <windows.h>

namespace
{
    void Require(bool value, const char* message)
    {
        if (value == false) {
            throw std::runtime_error(message);
        }
    }
    void Write(const std::filesystem::path& path, const std::string& text)
    {
        auto stream = std::ofstream(path, std::ios::binary);
        stream << text;
        Require(stream.good(), "fixture write failed");
    }
    std::string Read(const std::filesystem::path& path)
    {
        auto stream = std::ifstream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
} // namespace

int main(int argc, char** argv)
{
    try {
        Require(argc == 2, "profile test requires output directory");
        const auto root = std::filesystem::absolute(argv[1]) / std::to_string(GetTickCount64());
        const auto options = gengine::ProfileStorageOptions{true, root / L"日本語 空白"};
        auto first = gengine::SavedProfile{};
        first.game.schema = 1;
        first.game.count = 2;
        first.game.values[0] = 12;
        auto second = first;
        second.game.values[0] = 8;
        second.game.values[1] = 1;
        second.language = 1;
        second.display.mode = gengine::DisplayMode::kBorderless;
        second.display.resolution = {3840, 2160};
        second.display.vsync = false;
        second.display.monitor = "DISPLAY-TEST";
        second.audio = {40, 20, 60, true};
        second.input.keys[2] = 'Z';
        second.input.buttons[0] = 0x1000;
        {
            auto memory = gengine::ProfileStore("test-game", {false, root / "unused"});
            Require(memory.Load().has_value() == false && memory.Save(first), "memory profile failed");
            Require(std::filesystem::exists(root) == false, "memory profile wrote files");
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Save(first) == false, "save before load was accepted");
            Require(store.Load().has_value() == false && store.Save(first), "initial save failed");
            Require(store.Save(second), "second save failed");
            const auto modified = std::filesystem::last_write_time(store.directory() / "profile.json");
            Require(store.Save(second) &&
                        modified == std::filesystem::last_write_time(store.directory() / "profile.json"),
                    "identical save rewrote the file");
            auto other = gengine::ProfileStore("test-game", options);
            Require(other.Load().has_value() == false && other.status() == gengine::SaveStatus::kProtected &&
                        other.Save(first) == false,
                    "second process access was not protected");
        }
        {
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Load() == second, "reopen lost profile or language");
        }
        // 中断したtmpは無視し、正常な本体を優先する。
        Write(options.directory / "profile.json.tmp", "interrupted");
        {
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Load() == second, "interrupted tmp replaced the committed record");
        }
        Write(options.directory / "profile.json", "corrupt");
        {
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Load() == first && store.status() == gengine::SaveStatus::kRecovered,
                    "backup recovery failed");
            Require(store.Save(second), "recovered record could not be saved");
        }
        // 書込み失敗を表示し、ロック解除後の明示的な再試行で復旧する。
        {
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Load() == second, "recovery commit lost data");
            const auto path = options.directory / "profile.backup.json.tmp";
            const auto lock =
                CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(lock != INVALID_HANDLE_VALUE, "cannot lock test temporary file");
            const auto saved = store.Save(first);
            CloseHandle(lock);
            Require(saved == false && store.status() == gengine::SaveStatus::kFailed, "write failure was hidden");
            Require(store.Save(first), "retry after write failure failed");
        }
        // JSONが読めても、ゲームの値の検証に失敗した本体は正常な前回値から復旧する。
        Write(options.directory / "profile.json",
              "{\"format\":1,\"game\":\"test-game\",\"schema\":1,\"values\":[999,0],\"language\":0}");
        {
            auto store = gengine::ProfileStore("test-game", options);
            const auto recovered = store.Load(1, [](const auto& value) {
                return value.values[0] < 100;
            });
            Require(recovered == second && store.status() == gengine::SaveStatus::kRecovered,
                    "semantic recovery failed");
        }
        Write(options.directory / "profile.json",
              "{\"format\":1,\"game\":\"test-game\",\"schema\":2,\"values\":[12,0],\"language\":0}");
        {
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Load(1).has_value() == false && store.Save(first) == false, "future game schema overwritten");
        }
        // 旧ホスト形式は進行と言語を保ち、表示設定だけを既定値で補う。
        Write(options.directory / "profile.json",
              "{\"format\":1,\"game\":\"test-game\",\"schema\":1,\"values\":[12,0],\"language\":0}");
        {
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Load() == first && store.Save(second), "format 1 migration failed");
            Require(Read(options.directory / "profile.json").find("\"format\": 4") != std::string::npos,
                    "new save format not written");
            auto invalid = second;
            invalid.display.resolution = {123, 45};
            Require(store.Save(invalid) == false, "invalid display settings saved");
            invalid = second;
            invalid.audio.music = 101;
            Require(store.Save(invalid) == false, "invalid audio settings saved");
            invalid = second;
            invalid.input.keys[0] = invalid.input.keys[1];
            Require(store.Save(invalid) == false, "duplicate input bindings saved");
        }
        // 直前の形式2から表示設定を移行し、音量だけを補う。
        Write(
            options.directory / "profile.json",
            R"({"format":2,"game":"test-game","schema":1,"values":[8,1],"language":1,"display":{"mode":1,"width":3840,"height":2160,"vsync":false,"monitor":"DISPLAY-TEST"}})");
        {
            auto store = gengine::ProfileStore("test-game", options);
            auto migrated = second;
            migrated.audio = {};
            migrated.input = {};
            Require(store.Load() == migrated && store.Save(second), "format 2 migration failed");
        }
        Write(
            options.directory / "profile.json",
            R"({"format":3,"game":"test-game","schema":1,"values":[8,1],"language":1,"display":{"mode":1,"width":3840,"height":2160,"vsync":false,"monitor":"DISPLAY-TEST"},"audio":{"master":40,"music":20,"effects":60,"muted":true}})");
        {
            auto store = gengine::ProfileStore("test-game", options);
            auto migrated = second;
            migrated.input = {};
            Require(store.Load() == migrated && store.Save(second), "format 3 migration failed");
        }
        const auto future = std::string{"{\"format\":5,\"game\":\"test-game\"}"};
        Write(options.directory / "profile.json", future);
        {
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Load().has_value() == false && store.Save(first) == false, "future format overwritten");
        }
        Require(Read(options.directory / "profile.json") == future, "protected file changed");
        Write(options.directory / "profile.json", "broken");
        Write(options.directory / "profile.backup.json", "broken backup");
        {
            auto store = gengine::ProfileStore("test-game", options);
            Require(store.Load().has_value() == false && store.Save(first) == false, "unrecoverable data overwritten");
        }
        for (const auto* id : {"../escape", "", "Upper", "a/b", "a.b"}) {
            auto rejected = false;
            try {
                auto store = gengine::ProfileStore(id, {});
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            Require(rejected, "unsafe game id accepted");
        }
        std::cout << "profile store: isolation, restart, lock, atomic commit, recovery, retry and protection passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
