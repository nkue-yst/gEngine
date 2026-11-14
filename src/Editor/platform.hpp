#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

namespace gengine::editor
{
    // Windowsのハンドルは所有者を一つにし、失敗経路でも必ず閉じる。
    class Handle
    {
      public:
        Handle() = default;
        explicit Handle(HANDLE value) : value_(value)
        {
        }

        ~Handle()
        {
            Reset();
        }

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr))
        {
        }

        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other) {
                Reset(std::exchange(other.value_, nullptr));
            }
            return *this;
        }

        HANDLE Get() const
        {
            return value_;
        }

        bool Valid() const
        {
            return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
        }

        bool Close()
        {
            const auto value = std::exchange(value_, nullptr);
            return value == nullptr || value == INVALID_HANDLE_VALUE || CloseHandle(value) != FALSE;
        }

        void Reset(HANDLE value = nullptr)
        {
            if (Valid()) {
                CloseHandle(value_);
            }
            value_ = value;
        }

      private:
        HANDLE value_ = nullptr;
    };

    // コアと通信ではUTF-8、Windows APIではUTF-16を使う。不正な文字列は例外にする。
    std::wstring Wide(std::string_view text);
    std::string Utf8(std::wstring_view text);

    // 公開するパスはUTF-8とスラッシュ区切りへ統一する。
    std::string PathText(const std::filesystem::path& path);

    // 容量制限付きの読込みと、既存ファイルを上書きしない新規保存。
    std::string ReadFile(const std::filesystem::path& path, std::size_t limit);
    void WriteNewFile(const std::filesystem::path& path, std::string_view data);
    // 同じディレクトリに書き終えた一時ファイルだけを置換し、失敗時は旧ファイルを残す。
    void WriteAtomicFile(const std::filesystem::path& path, std::string_view data);

    // 入力・成果物の同一性はハッシュで、起動セッションはGUIDで識別する。
    std::string Sha256(std::string_view data);
    std::string NewId();

    // CreateProcessへ直接渡す一引数を引用する。シェル用のエスケープではない。
    std::wstring QuoteArgument(std::wstring_view value);
} // namespace gengine::editor
